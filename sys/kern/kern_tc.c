/* $NetBSD: kern_tc.c,v 1.62 2021/06/02 21:34:58 riastradh Exp $ */

/*-
 * Copyright (c) 2008, 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ---------------------------------------------------------------------------
 */

#include <sys/cdefs.h>
/*
__FBSDID("$FreeBSD: src/sys/kern/kern_tc.c,v 1.166 2005/09/19 22:16:31 andre Exp $");
__KERNEL_RCSID(0, "$NetBSD: kern_tc.c,v 1.62 2021/06/02 21:34:58 riastradh Exp $");
*/
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/timepps.h>
#include <sys/timetc.h>
#include <sys/time.h>

/*
 * A large step happens on boot.  This constant detects such steps.
 * It is relatively small so that ntp_update_second gets called enough
 * in the typical 'missed a couple of seconds' case, but doesn't loop
 * forever when the time step is large.
 */
#define LARGE_STEP	200

/*
 * Implement a dummy timecounter which we can use until we get a real one
 * in the air.  This allows the console and other early stuff to use
 * time services.
 */

static u_int
dummy_get_timecount(tc)
	struct timecounter *tc;
{
	static u_int now;

	return ++now;
}

static struct timecounter dummy_timecounter = {
		.tc_get_timecount	= dummy_get_timecount,
		.tc_counter_mask	= ~0u,
		.tc_frequency		= 1000000,
		.tc_name			= "dummy",
		.tc_quality			= -1000000,
		.tc_priv			= NULL,
};

struct timehands {
	/* These fields must be initialized by the driver. */
	struct timecounter	*th_counter;     /* active timecounter */
	int64_t				th_adjustment;   /* frequency adjustment */
						 /* (NTP/adjtime) */
	uint64_t			th_scale;        /* scale factor (counter */
						 /* tick->time) */
	uint64_t 			th_offset_count; /* offset at last time */
						 /* update (tc_windup()) */
	struct bintime		th_offset;       /* bin (up)time at windup */
	struct timeval		th_microtime;    /* cached microtime */
	struct timespec		th_nanotime;     /* cached nanotime */
	/* Fields not to be copied in tc_windup start with th_generation. */
	volatile u_int		th_generation;   /* current genration */
	struct timehands	*th_next;        /* next timehand */
};

static struct timehands th0;
static struct timehands th9 = { .th_next = &th0, };
static struct timehands th8 = { .th_next = &th9, };
static struct timehands th7 = { .th_next = &th8, };
static struct timehands th6 = { .th_next = &th7, };
static struct timehands th5 = { .th_next = &th6, };
static struct timehands th4 = { .th_next = &th5, };
static struct timehands th3 = { .th_next = &th4, };
static struct timehands th2 = { .th_next = &th3, };
static struct timehands th1 = { .th_next = &th2, };

static struct timehands th0 = {
	.th_counter = &dummy_timecounter,
	.th_scale = (uint64_t)-1 / 1000000,
	.th_offset = { .sec = 1, .frac = 0 },
	.th_generation = 1,
	.th_next = &th1,
};

static struct timehands *volatile timehands = &th0;
struct timecounter *timecounter = &dummy_timecounter;
static struct timecounter *timecounters = &dummy_timecounter;

volatile time_t time_second = 1;
volatile time_t time_uptime = 1;

static struct bintime timebasebin;

static int timestepwarnings;

struct lock_object timecounter_lock;
static u_int timecounter_mods;
static volatile int timecounter_removals = 1;
static u_int timecounter_bad;

#ifdef TC_COUNTERS
#define	TC_STATS(name)												\
static struct evcnt n##name =										\
    EVCNT_INITIALIZER(EVCNT_TYPE_MISC, NULL, "timecounter", #name);	\
EVCNT_ATTACH_STATIC(n##name)
TC_STATS(binuptime);    TC_STATS(nanouptime);    TC_STATS(microuptime);
TC_STATS(bintime);      TC_STATS(nanotime);      TC_STATS(microtime);
TC_STATS(getbinuptime); TC_STATS(getnanouptime); TC_STATS(getmicrouptime);
TC_STATS(getbintime);   TC_STATS(getnanotime);   TC_STATS(getmicrotime);
TC_STATS(setclock);
#define	TC_COUNT(var)	var.ev_count++
#undef TC_STATS
#else
#define	TC_COUNT(var)	/* nothing */
#endif	/* TC_COUNTERS */

static void tc_windup(void);

/*
 * Return the difference between the timehands' counter value now and what
 * was when we copied it to the timehands' offset_count.
 */
static inline u_int
tc_delta(th)
	struct timehands *th;
{
	struct timecounter *tc;

	tc = th->th_counter;
	return (tc->tc_get_timecount(tc) - th->th_offset_count) & tc->tc_counter_mask;
}

/*
 * Functions for reading the time.  We have to loop until we are sure that
 * the timehands that we operated on was not updated under our feet.  See
 * the comment in <sys/timevar.h> for a description of these 12 functions.
 */

void
binuptime(bt)
	struct bintime *bt;
{
	struct timehands *th;
	struct proc *p;
	u_int lgen, gen;

	TC_COUNT(nbinuptime);

	/*
	 * Provide exclusion against tc_detach().
	 *
	 * We record the number of timecounter removals before accessing
	 * timecounter state.  Note that the LWP can be using multiple
	 * "generations" at once, due to interrupts (interrupted while in
	 * this function).  Hardware interrupts will borrow the interrupted
	 * LWP's l_tcgen value for this purpose, and can themselves be
	 * interrupted by higher priority interrupts.  In this case we need
	 * to ensure that the oldest generation in use is recorded.
	 *
	 * splsched() is too expensive to use, so we take care to structure
	 * this code in such a way that it is not required.  Likewise, we
	 * do not disable preemption.
	 *
	 * Memory barriers are also too expensive to use for such a
	 * performance critical function.  The good news is that we do not
	 * need memory barriers for this type of exclusion, as the thread
	 * updating timecounter_removals will issue a broadcast cross call
	 * before inspecting our l_tcgen value (this elides memory ordering
	 * issues).
	 */
	p = curproc;
	lgen = p->p_tcgen;
	if (__predict_true(lgen == 0)) {
		p->p_tcgen = timecounter_removals;
	}
	__insn_barrier();

	do {
		th = timehands;
		gen = th->th_generation;
		*bt = th->th_offset;
		bintime_addx(bt, th->th_scale * tc_delta(th));
	} while (gen == 0 || gen != th->th_generation);

	__insn_barrier();
	p->p_tcgen = lgen;
}

void
nanouptime(tsp)
	struct timespec *tsp;
{
	struct bintime bt;

	TC_COUNT(nnanouptime);
	binuptime(&bt);
	bintime2timespec(&bt, tsp);
}

void
microuptime(tvp)
	struct timeval *tvp;
{
	struct bintime bt;

	TC_COUNT(nmicrouptime);
	binuptime(&bt);
	bintime2timeval(&bt, tvp);
}

void
bintime(bt)
	struct bintime *bt;
{

	TC_COUNT(nbintime);
	binuptime(bt);
	bintime_add(bt, &timebasebin);
}

void
nanotime(tsp)
	struct timespec *tsp;
{
	struct bintime bt;

	TC_COUNT(nnanotime);
	bintime(&bt);
	bintime2timespec(&bt, tsp);
}

void
microtime(tvp)
	struct timeval *tvp;
{
	struct bintime bt;

	TC_COUNT(nmicrotime);
	bintime(&bt);
	bintime2timeval(&bt, tvp);
}

void
getbinuptime(bt)
	struct bintime *bt;
{
	struct timehands *th;
	u_int gen;

	TC_COUNT(ngetbinuptime);
	do {
		th = timehands;
		gen = th->th_generation;
		*bt = th->th_offset;
	} while (gen == 0 || gen != th->th_generation);
}

void
getnanouptime(tsp)
	struct timespec *tsp;
{
	struct timehands *th;
	u_int gen;

	TC_COUNT(ngetnanouptime);
	do {
		th = timehands;
		gen = th->th_generation;
		bintime2timespec(&th->th_offset, tsp);
	} while (gen == 0 || gen != th->th_generation);
}

void
getmicrouptime(tvp)
	struct timeval *tvp;
{
	struct timehands *th;
	u_int gen;

	TC_COUNT(ngetmicrouptime);
	do {
		th = timehands;
		gen = th->th_generation;
		bintime2timeval(&th->th_offset, tvp);
	} while (gen == 0 || gen != th->th_generation);
}

void
getbintime(bt)
	struct bintime *bt;
{
	struct timehands *th;
	u_int gen;

	TC_COUNT(ngetbintime);
	do {
		th = timehands;
		gen = th->th_generation;
		*bt = th->th_offset;
	} while (gen == 0 || gen != th->th_generation);
	bintime_add(bt, &timebasebin);
}

static inline void
dogetnanotime(tsp)
	struct timespec *tsp;
{
	struct timehands *th;
	u_int gen;

	TC_COUNT(ngetnanotime);
	do {
		th = timehands;
		gen = th->th_generation;
		*tsp = th->th_nanotime;
	} while (gen == 0 || gen != th->th_generation);
}

void
getnanotime(tsp)
	struct timespec *tsp;
{
	dogetnanotime(tsp);
}

void
getmicrotime(tvp)
	struct timeval *tvp;
{
	struct timehands *th;
	u_int gen;

	TC_COUNT(ngetmicrotime);
	do {
		th = timehands;
		gen = th->th_generation;
		*tvp = th->th_microtime;
	} while (gen == 0 || gen != th->th_generation);
}

void
getnanoboottime(tsp)
	struct timespec *tsp;
{
	struct bintime bt;

	getbinboottime(&bt);
	bintime2timespec(&bt, tsp);
}

void
getmicroboottime(tvp)
	struct timeval *tvp;
{
	struct bintime bt;

	getbinboottime(&bt);
	bintime2timeval(&bt, tvp);
}

void
getbinboottime(bt)
	struct bintime *bt;
{

	/*
	 * XXX Need lockless read synchronization around timebasebin
	 * (and not just here).
	 */
	*bt = timebasebin;
}

/*
 * Initialize a new timecounter and possibly use it.
 */
void
tc_init(tc)
	struct timecounter *tc;
{
	u_int u;

	KASSERTMSG(tc->tc_next == NULL, "timecounter %s already initialised", tc->tc_name);

	u = tc->tc_frequency / tc->tc_counter_mask;
	/* XXX: We need some margin here, 10% is a guess */
	u *= 11;
	u /= 10;
	if (u > hz && tc->tc_quality >= 0) {
		tc->tc_quality = -2000;
		printf("timecounter: Timecounter \"%s\" frequency %ju Hz", tc->tc_name,
				(uintmax_t) tc->tc_frequency);
		printf(" -- Insufficient hz, needs at least %u\n", u);
	} else if (tc->tc_quality >= 0 || bootverbose) {
		printf("timecounter: Timecounter \"%s\" frequency %ju Hz "
				"quality %d\n", tc->tc_name, (uintmax_t) tc->tc_frequency,
				tc->tc_quality);
	}

	simple_lock(&timecounter_lock);
	tc->tc_next = timecounters;
	timecounters = tc;
	timecounter_mods++;
	/*
	 * Never automatically use a timecounter with negative quality.
	 * Even though we run on the dummy counter, switching here may be
	 * worse since this timecounter may not be monotonous.
	 */
	if (tc->tc_quality >= 0
			&& (tc->tc_quality > timecounter->tc_quality
					|| (tc->tc_quality == timecounter->tc_quality
							&& tc->tc_frequency > timecounter->tc_frequency))) {
		(void) tc->tc_get_timecount(tc);
		(void) tc->tc_get_timecount(tc);
		timecounter = tc;
		tc_windup();
	}
	simple_unlock(&timecounter_lock);
}

/*
 * Pick a new timecounter due to the existing counter going bad.
 */
static void
tc_pick(void)
{
	struct timecounter *best, *tc;

	for (best = tc = timecounters; tc != NULL; tc = tc->tc_next) {
		if (tc->tc_quality > best->tc_quality)
			best = tc;
		else if (tc->tc_quality < best->tc_quality)
			continue;
		else if (tc->tc_frequency > best->tc_frequency)
			best = tc;
	}
	(void)best->tc_get_timecount(best);
	(void)best->tc_get_timecount(best);
	timecounter = best;
}

/*
 * A timecounter has gone bad, arrange to pick a new one at the next
 * clock tick.
 */
void
tc_gonebad(tc)
	struct timecounter *tc;
{

	tc->tc_quality = -100;
	membar_producer();
	atomic_inc_int(&timecounter_bad);
}

/*
 * Stop using a timecounter and remove it from the timecounters list.
 */
int
tc_detach(target)
	struct timecounter *target;
{
	struct timecounter *tc;
	struct timecounter **tcp = NULL;
	int removals;
	struct proc *p;

	/* First, find the timecounter. */
	simple_lock(&timecounter_lock);

	for (tcp = &timecounters, tc = timecounters;
	     tc != NULL;
	     tcp = &tc->tc_next, tc = tc->tc_next) {
		if (tc == target)
			break;
	}
	if (tc == NULL) {
		simple_unlock(&timecounter_lock);
		return ESRCH;
	}

	/* And now, remove it. */
	*tcp = tc->tc_next;
	if (timecounter == target) {
		tc_pick();
		tc_windup();
	}
	timecounter_mods++;
	removals = timecounter_removals++;
	simple_unlock(&timecounter_lock);
	return (0);
}

/* Report the frequency of the current timecounter. */
uint64_t
tc_getfrequency(void)
{
	return (timehands->th_counter->tc_frequency);
}

/*
 * Step our concept of UTC.  This is done by modifying our estimate of
 * when we booted.
 */
void
tc_setclock(ts)
	struct timespec *ts;
{
	struct timespec ts2;
	struct bintime bt, bt2;

	simple_lock(&timecounter_lock);
	TC_COUNT(nsetclock);
	binuptime(&bt2);
	timespec2bintime(ts, &bt);
	bintime_sub(&bt, &bt2);
	bintime_add(&bt2, &timebasebin);
	timebasebin = bt;
	tc_windup();
	simple_unlock(&timecounter_lock);

	if (timestepwarnings) {
		bintime2timespec(&bt2, &ts2);
		log(LOG_INFO, "Time stepped from %jd.%09ld to %jd.%09ld\n",
		    (intmax_t)ts2.tv_sec, ts2.tv_nsec,
		    (intmax_t)ts->tv_sec, ts->tv_nsec);
	}
}

/*
 * Skew the timehands according to any adjtime(2) adjustment.
 */
void
ntp_update_second(adjustment, newsec)
	int64_t *adjustment;
	time_t *newsec;
{
	int64_t adj;

	if ((*newsec) % 86400 == 0) {
		(*newsec)--;
	} else if (((*newsec) + 1) % 86400 == 0) {
		(*newsec)++;
	}

	if (adjustment > 0) {
		adj = MIN(5000, *adjustment);
	} else {
		adj = MAX(-5000, *adjustment);
	}
	*adjustment -= adj;
	*adjustment = (adj * 1000) << 32;
}

/*
 * ratecheck(): simple time-based rate-limit checking.  see ratecheck(9)
 * for usage and rationale.
 */
int
ratecheck(lasttime, mininterval)
	struct timeval *lasttime;
	const struct timeval *mininterval;
{
	struct timeval tv, delta;
	int rv = 0;

	getmicrouptime(&tv);
	timersub(&tv, lasttime, &delta);

	/*
	 * check for 0,0 is so that the message will be seen at least once,
	 * even if interval is huge.
	 */
	if (timercmp(&delta, mininterval, >=) ||
	    (lasttime->tv_sec == 0 && lasttime->tv_usec == 0)) {
		*lasttime = tv;
		rv = 1;
	}

	return (rv);
}

/*
 * ppsratecheck(): packets (or events) per second limitation.
 */
int
ppsratecheck(lasttime, curpps, maxpps)
	struct timeval *lasttime;
	int *curpps;
	int maxpps;
{
	struct timeval tv, delta;
	int rv;

	getmicrouptime(&tv);
	timersub(&tv, lasttime, &delta);

	/*
	 * check for 0,0 is so that the message will be seen at least once.
	 * if more than one second have passed since the last update of
	 * lasttime, reset the counter.
	 *
	 * we do increment *curpps even in *curpps < maxpps case, as some may
	 * try to use *curpps for stat purposes as well.
	 */
	if ((lasttime->tv_sec == 0 && lasttime->tv_usec == 0) ||
	    delta.tv_sec >= 1) {
		*lasttime = tv;
		*curpps = 0;
	}
	if (maxpps < 0)
		rv = 1;
	else if (*curpps < maxpps)
		rv = 1;
	else
		rv = 0;

#if 1 /*DIAGNOSTIC?*/
	/* be careful about wrap-around */
	if (__predict_true(*curpps != INT_MAX))
		*curpps = *curpps + 1;
#else
	/*
	 * assume that there's not too many calls to this function.
	 * not sure if the assumption holds, as it depends on *caller's*
	 * behavior, not the behavior of this function.
	 * IMHO it is wrong to make assumption on the caller's behavior,
	 * so the above #if is #if 1, not #ifdef DIAGNOSTIC.
	 */
	*curpps = *curpps + 1;
#endif

	return (rv);
}

/*
 * Initialize the next struct timehands in the ring and make
 * it the active timehands.  Along the way we might switch to a different
 * timecounter and/or do seconds processing in NTP.  Slightly magic.
 */
static void
tc_windup(void)
{
	struct bintime bt;
	struct timehands *th, *tho;
	u_int64_t scale;
	u_int delta, ncount, ogen;
	int i, s_update;
	time_t t;

	s_update = 0;

	/*
	 * Make the next timehands a copy of the current one, but do not
	 * overwrite the generation or next pointer.  While we update
	 * the contents, the generation must be zero.  Ensure global
	 * visibility of the generation before proceeding.
	 */
	tho = timehands;
	th = tho->th_next;
	ogen = th->th_generation;
	th->th_generation = 0;
	membar_producer();
	bcopy(tho, th, offsetof(struct timehands, th_generation));

	/*
	 * Capture a timecounter delta on the current timecounter and if
	 * changing timecounters, a counter value from the new timecounter.
	 * Update the offset fields accordingly.
	 */
	delta = tc_delta(th);
	if (th->th_counter != timecounter)
		ncount = timecounter->tc_get_timecount(timecounter);
	else
		ncount = 0;
	th->th_offset_count += delta;
	th->th_offset_count &= th->th_counter->tc_counter_mask;
	bintime_addx(&th->th_offset, th->th_scale * delta);

	/*
	 * Hardware latching timecounters may not generate interrupts on
	 * PPS events, so instead we poll them.  There is a finite risk that
	 * the hardware might capture a count which is later than the one we
	 * got above, and therefore possibly in the next NTP second which might
	 * have a different rate than the current NTP second.  It doesn't
	 * matter in practice.
	 */
	if (tho->th_counter->tc_poll_pps)
		tho->th_counter->tc_poll_pps(tho->th_counter);

	/*
	 * Deal with NTP second processing.  The for loop normally
	 * iterates at most once, but in extreme situations it might
	 * keep NTP sane if timeouts are not run for several seconds.
	 * At boot, the time step can be large when the TOD hardware
	 * has been read, so on really large steps, we call
	 * ntp_update_second only twice.  We need to call it twice in
	 * case we missed a leap second.
	 * If NTP is not compiled in ntp_update_second still calculates
	 * the adjustment resulting from adjtime() calls.
	 */
	bt = th->th_offset;
	bintime_add(&bt, &timebasebin);
	i = bt.sec - tho->th_microtime.tv_sec;
	if (i > LARGE_STEP)
		i = 2;
	for (; i > 0; i--) {
		t = bt.sec;
		ntp_update_second(&th->th_adjustment, &bt.sec);
		s_update = 1;
		if (bt.sec != t)
			timebasebin.sec += bt.sec - t;
	}

	/* Update the UTC timestamps used by the get*() functions. */
	/* XXX shouldn't do this here.  Should force non-`get' versions. */
	bintime2timeval(&bt, &th->th_microtime);
	bintime2timespec(&bt, &th->th_nanotime);

	/* Now is a good time to change timecounters. */
	if (th->th_counter != timecounter) {
		th->th_counter = timecounter;
		th->th_offset_count = ncount;
		s_update = 1;
	}

	/*-
	 * Recalculate the scaling factor.  We want the number of 1/2^64
	 * fractions of a second per period of the hardware counter, taking
	 * into account the th_adjustment factor which the NTP PLL/adjtime(2)
	 * processing provides us with.
	 *
	 * The th_adjustment is nanoseconds per second with 32 bit binary
	 * fraction and we want 64 bit binary fraction of second:
	 *
	 *	 x = a * 2^32 / 10^9 = a * 4.294967296
	 *
	 * The range of th_adjustment is +/- 5000PPM so inside a 64bit int
	 * we can only multiply by about 850 without overflowing, but that
	 * leaves suitably precise fractions for multiply before divide.
	 *
	 * Divide before multiply with a fraction of 2199/512 results in a
	 * systematic undercompensation of 10PPM of th_adjustment.  On a
	 * 5000PPM adjustment this is a 0.05PPM error.  This is acceptable.
 	 *
	 * We happily sacrifice the lowest of the 64 bits of our result
	 * to the goddess of code clarity.
	 *
	 */
	if (s_update) {
		scale = (u_int64_t)1 << 63;
		scale += (th->th_adjustment / 1024) * 2199;
		scale /= th->th_counter->tc_frequency;
		th->th_scale = scale * 2;
	}
	/*
	 * Now that the struct timehands is again consistent, set the new
	 * generation number, making sure to not make it zero.  Ensure
	 * changes are globally visible before changing.
	 */
	if (++ogen == 0)
		ogen = 1;
	membar_producer();
	th->th_generation = ogen;

	/*
	 * Go live with the new struct timehands.  Ensure changes are
	 * globally visible before changing.
	 */
	time_second = th->th_microtime.tv_sec;
	time_uptime = th->th_offset.sec;
	membar_producer();
	timehands = th;

	/*
	 * Force users of the old timehand to move on.  This is
	 * necessary for MP systems; we need to ensure that the
	 * consumers will move away from the old timehand before
	 * we begin updating it again when we eventually wrap
	 * around.
	 */
	if (++tho->th_generation == 0)
		tho->th_generation = 1;
}

/*
 * Timecounters need to be updated every so often to prevent the hardware
 * counter from overflowing.  Updating also recalculates the cached values
 * used by the get*() family of functions, so their precision depends on
 * the update frequency.
 */

static int tc_tick;

void
tc_ticktock(void)
{
	static int count;

	if (++count < tc_tick)
		return;
	count = 0;
	simple_lock(&timecounter_lock);
	if (timecounter_bad != 0) {
		/* An existing timecounter has gone bad, pick a new one. */
		(void)atomic_swap_uint(&timecounter_bad, 0);
		if (timecounter->tc_quality < 0) {
			tc_pick();
		}
	}
	tc_windup();
	simple_unlock(&timecounter_lock);
}

void
inittimecounter(void)
{
	u_int p;

	simple_lock_init(&timecounter_lock, "timecounter_lock");

	/*
	 * Set the initial timeout to
	 * max(1, <approx. number of hardclock ticks in a millisecond>).
	 * People should probably not use the sysctl to set the timeout
	 * to smaller than its inital value, since that value is the
	 * smallest reasonable one.  If they want better timestamps they
	 * should use the non-"get"* functions.
	 */
	if (hz > 1000)
		tc_tick = (hz + 500) / 1000;
	else
		tc_tick = 1;
	p = (tc_tick * 1000000) / hz;
	printf("timecounter: Timecounters tick every %d.%03u msec\n",
	    p / 1000, p % 1000);

	/* warm up new timecounter (again) and get rolling. */
	(void)timecounter->tc_get_timecount(timecounter);
	(void)timecounter->tc_get_timecount(timecounter);
}

/* Report or change the active timecounter hardware. */
static int
sysctl_timecounter_hardware(oldp, oldlenp, newp, newlen)
	void *oldp, *newp;
	size_t *oldlenp;
	size_t newlen;
{
	char newname[32];
	struct timecounter *newtc, *tc;
	int error;

	tc = timecounter;
	strlcpy(newname, tc->tc_name, sizeof(newname));

	error = sysctl_string(oldp, oldlenp, newp, newlen, newname, sizeof(newname));
	if (error != 0 || strcmp(newname, tc->tc_name) == 0) {
		return (error);
	}
	if (!cold) {
		simple_lock(&timecounter_lock);
	}
	error = EINVAL;
	for (newtc = timecounters; newtc != NULL; newtc = newtc->tc_next) {
		if (strcmp(newname, newtc->tc_name) != 0) {
			continue;
		}
		/* Warm up new timecounter. */
		(void)newtc->tc_get_timecount(newtc);
		(void)newtc->tc_get_timecount(newtc);
		timecounter = newtc;
		error = 0;
		break;
	}
	if (!cold) {
		simple_unlock(&timecounter_lock);
	}
	return (error);
}

/* Report or change the active timecounter hardware. */
static int
sysctl_timecounter_choice(namelen, oldp, oldlenp, newp, newlen)
	u_int namelen;
	void *oldp, *newp;
	size_t *oldlenp;
	size_t newlen;
{
	char buf[32], *spc, *choices;
	struct timecounter *tc;
	size_t needed, left, slen;
    void *where;
	int error, mods;

	if (newp != NULL) {
		return (EPERM);
	}
	if (namelen != 0) {
		return (EINVAL);
	}

	simple_lock(&timecounter_lock);

retry:
	spc = "";
	error = 0;
	needed = 0;
	left = *oldlenp;
	where = oldp;
	for (tc = timecounters; error == 0 && tc != NULL; tc = tc->tc_next) {
		if (where == NULL) {
			needed += sizeof(buf); /* be conservative */
		} else {
			slen = snprintf(buf, sizeof(buf), "%s%s(q=%d, f=%" PRId64
					" Hz)", spc, tc->tc_name, tc->tc_quality,
					tc->tc_frequency);
			if (left < slen + 1)
				break;
			mods = timecounter_mods;
			simple_unlock(&timecounter_lock);
			error = copyout(buf, where, slen + 1);
			simple_lock(&timecounter_lock);
			if (mods != timecounter_mods) {
				goto retry;
			}
			spc = " ";
			where += slen;
			needed += slen;
			left -= slen;
		}
	}
	simple_unlock(&timecounter_lock);

	*oldlenp = needed;
	return (error);
}

/*
 * Return timecounter-related information.
 */
int
sysctl_timecounter(name, namelen, oldp, oldlenp, newp, newlen)
	int *name;
	u_int namelen;
	void *oldp, *newp;
	size_t *oldlenp;
	size_t newlen;
{
	int error;

	if (namelen != 1) {
		return (ENOTDIR);
	}

	switch (name[0]) {
	case KERN_TIMECOUNTER_HARDWARE:
		error = sysctl_timecounter_hardware(oldp, oldlenp, newp, newlen);
		break;

	case KERN_TIMECOUNTER_CHOICE:
		error = sysctl_timecounter_choice(namelen, oldp, oldlenp, newp, newlen);
		break;
	}
	return (error);
}
