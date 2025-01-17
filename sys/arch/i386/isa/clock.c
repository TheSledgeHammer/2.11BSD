/*	$NetBSD: clock.c,v 1.44.8.1 1998/11/10 04:17:55 cgd Exp $	*/

/*-
 * Copyright (c) 1993, 1994 Charles Hannum.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)clock.c	7.2 (Berkeley) 5/12/91
 */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
  Copyright 1988, 1989 by Intel Corporation, Santa Clara, California.

		All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appears in all
copies and that both the copyright notice and this permission notice
appear in supporting documentation, and that the name of Intel
not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

INTEL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
IN NO EVENT SHALL INTEL BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/*
 * Primitive clock interrupt routines.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/device.h>

#include <dev/audio/speaker/spkrio.h>
#include <dev/audio/speaker/pcppireg.h>
#include <dev/core/ic/mc146818reg.h>
#include <dev/core/ic/i8253reg.h>
#include <dev/core/isa/isareg.h>
#include <dev/core/isa/isavar.h>
#include <dev/core/isa/rtc.h>

#include <i386/isa/nvram.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/pio.h>
#include <machine/cpufunc.h>

#include "config_time.h"			/* for CONFIG_TIME */

#include "mca.h"
#if NMCA > 0
#include <machine/mca/mca_machdep.h>	/* for MCA_system */
#endif

#include "pcppi.h"
#if (NPCPPI > 0)
#include <dev/audio/speaker/pcppivar.h>

#ifdef CLOCKDEBUG
int clock_debug = 0;
#define DPRINTF(arg) if (clock_debug) printf arg
#else
#define DPRINTF(arg)
#endif

int sysbeepmatch(struct device *, struct cfdata *, void *);
void sysbeepattach(struct device *, struct device *, void *);

CFOPS_DECL(sysbeep, sysbeepmatch, sysbeepattach, NULL, NULL);
CFDRIVER_DECL(NULL, sysbeep, DV_DULL);
CFATTACH_DECL(sysbeep, &sysbeep_cd, &sysbeep_cops, sizeof(struct device));

static int 			ppi_attached;
static pcppi_tag_t 	ppicookie;
static int 			beeping;
#endif /* PCPPI */

void		findcpuspeed(void);
int			clockintr(void *);
int			gettick(void);
void		sysbeepstop(void *);
void		sysbeep(int, int);
void		rtcinit(void);
int			rtcget(mc_todregs *);
void		rtcput(mc_todregs *);
static int 	yeartoday(int);
int 		bcdtobin(int);
int			bintobcd(int);

__inline u_int mc146818_read(void *, u_int);
__inline void mc146818_write(void *, u_int, u_int);

#define	SECMIN	((unsigned)60)				/* seconds per minute */
#define	SECHOUR	((unsigned)(60*SECMIN))		/* seconds per hour */
#define	SECDAY	((unsigned)(24*SECHOUR))	/* seconds per day */
#define	SECYR	((unsigned)(365*SECDAY))	/* seconds per common year */

__inline u_int
mc146818_read(sc, reg)
	void *sc;					/* XXX use it? */
	u_int reg;
{
	outb(IO_RTC, reg);
	return (inb(IO_RTC+1));
}

__inline void
mc146818_write(sc, reg, datum)
	void *sc;					/* XXX use it? */
	u_int reg, datum;
{
	outb(IO_RTC, reg);
	outb(IO_RTC+1, datum);
}

/*
 * microtime() makes use of the following globals.  Note that isa_timer_tick
 * may be redundant to the `tick' variable, but is kept here for stability.
 * isa_timer_count is the countdown count for the timer.  timer_msb_table[]
 * and timer_lsb_table[] are used to compute the microsecond increment
 * for time.tv_usec in the follow fashion:
 *
 * time.tv_usec += isa_timer_msb_table[cnt_msb] - isa_timer_lsb_table[cnt_lsb];
 */
#define	ISA_TIMER_MSB_TABLE_SIZE	128

u_long	isa_timer_tick;									/* the number of microseconds in a tick */
u_short	isa_timer_count;								/* the countdown count for the timer */
u_short	isa_timer_msb_table[ISA_TIMER_MSB_TABLE_SIZE];	/* timer->usec MSB */
u_short	isa_timer_lsb_table[256];						/* timer->usec conversion for LSB */

void
startrtclock()
{
	int s;
	u_long tval;
	u_long t, msb, lsb, quotient, remainder;

	findcpuspeed();		/* use the clock (while it's free) to find the cpu speed */

	/*
	 * Compute timer_tick from hz.  We truncate this value (i.e.
	 * round down) to minimize the possibility of a backward clock
	 * step if hz is not a nice number.
	 */
	isa_timer_tick = 1000000 / (u_long) hz;

	/*
	 * Compute timer_count, the count-down count the timer will be
	 * set to.  We can't stand any number with an MSB larger than
	 * TIMER_MSB_TABLE_SIZE will accomodate.  Also, correctly round
	 * this by carrying an extra bit through the division.
	 */
	tval = (TIMER_FREQ * 2) / (u_long) hz;
	tval = (tval / 2) + (tval & 0x1);
	if ((tval / 256) >= ISA_TIMER_MSB_TABLE_SIZE
	    || TIMER_FREQ > (8*1024*1024)) {
		panic("startrtclock: TIMER_FREQ/HZ unsupportable");
	}
	isa_timer_count = (u_short) tval;

	/*
	 * Now compute the translation tables from timer ticks to
	 * microseconds.  We go to some length to ensure all values
	 * are rounded-to-nearest (i.e. +-0.5 of the exact values)
	 * as this will ensure the computation
	 *
	 * isa_timer_msb_table[msb] - isa_timer_lsb_table[lsb]
	 *
	 * will produce a result which is +-1 usec away from the
	 * correctly rounded conversion (in fact, it'll be exact about
	 * 75% of the time, 1 too large 12.5% of the time, and 1 too
	 * small 12.5% of the time).
	 */
	for (s = 0; s < 256; s++) {
		/* LSB table is easy, just divide and round */
		t = ((u_long) s * 1000000 * 2) / TIMER_FREQ;
		isa_timer_lsb_table[s] = (u_short) ((t / 2) + (t & 0x1));

		/* MSB table is zero unless the MSB is <= isa_timer_count */
		if (s < ISA_TIMER_MSB_TABLE_SIZE) {
			msb = ((u_long) s) * 256;
			if (msb > tval) {
				isa_timer_msb_table[s] = 0;
			} else {
				/*
				 * Harder computation here, since multiplying
				 * the value by 1000000 can overflow a long.
				 * To avoid 64-bit computations we divide
				 * the high order byte and the low order
				 * byte of the numerator separately, adding
				 * the remainder of the first computation
				 * into the second.  The constraint on
				 * TIMER_FREQ above should prevent overflow
				 * here.
				 */
				msb = tval - msb;
				lsb = msb % 256;
				msb = (msb / 256) * 1000000;
				quotient = msb / TIMER_FREQ;
				remainder = msb % TIMER_FREQ;
				t = ((remainder * 256 * 2)
				    + (lsb * 1000000 * 2)) / TIMER_FREQ;
				isa_timer_msb_table[s] = (u_short)((t / 2)
				    + (t & 0x1) + (quotient * 256));
			}
		}
	}

	/* initialize 8253 clock */
	outb(TIMER_MODE, TIMER_SEL0|TIMER_RATEGEN|TIMER_16BIT);

	/* Correct rounding will buy us a better precision in timekeeping */
	outb(IO_TIMER1, isa_timer_count % 256);
	outb(IO_TIMER1, isa_timer_count / 256);

	/* Check diagnostic status */
	if ((s = mc146818_read(NULL, RTC_DIAG)) != 0) { /* XXX softc */
		char bits[128];
		printf("RTC BIOS diagnostic error %s\n",
		    bitmask_snprintf(s, RTCDG_BITS, bits, sizeof(bits)));
	}
}

int
clockintr(arg)
	void *arg;
{
	struct clockframe *frame = arg;		/* not strictly necessary */

	hardclock(frame, arg);
	return (-1);
}

int
gettick()
{
	u_char lo, hi;

	/* Don't want someone screwing with the counter while we're here. */
	disable_intr();
	/* Select counter 0 and latch it. */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
	lo = inb(TIMER_CNTR0);
	hi = inb(TIMER_CNTR0);
	enable_intr();
	return ((hi << 8) | lo);
}

/*
 * Wait "n" microseconds.
 * Relies on timer 1 counting down from (TIMER_FREQ / hz) at TIMER_FREQ Hz.
 * Note: timer had better have been programmed before this is first used!
 * (Note that we use `rate generator' mode, which counts at 1:1; `square
 * wave' mode counts at 2:1).
 */
void
i8254_delay(n)
	int n;
{
	int limit, tick, otick;
	static const int delaytab[26] = {
		 0,  2,  3,  4,  5,  6,  7,  9, 10, 11,
		12, 13, 15, 16, 17, 18, 19, 21, 22, 23,
		24, 25, 27, 28, 29, 30,
	};

	/*
	 * Read the counter first, so that the rest of the setup overhead is
	 * counted.
	 */
	otick = gettick();

	if (n <= 25) {
			n = delaytab[n];
	} else {
#ifdef __GNUC__
		/*
		 * Calculate ((n * TIMER_FREQ) / 1e6) using explicit assembler code so
		 * we can take advantage of the intermediate 64-bit quantity to prevent
		 * loss of significance.
		 */
		int m;
		__asm __volatile(
				"mul %3"
				: "=a" (n), "=d" (m)
				: "0" (n), "r" (TIMER_FREQ)
		);
		__asm __volatile(
				"div %4"
				: "=a" (n), "=d" (m)
				: "0" (n), "1" (m), "r" (1000000)
		);
#else
		/*
		 * Calculate ((n * TIMER_FREQ) / 1e6) without using floating point and
		 * without any avoidable overflows.
		 */
		int sec = n / 1000000,
			usec = n % 1000000;
		n = sec * TIMER_FREQ +
			usec * (TIMER_FREQ / 1000000) +
			usec * ((TIMER_FREQ % 1000000) / 1000) / 1000 +
			usec * (TIMER_FREQ % 1000) / 1000000;
#endif
	}

	limit = TIMER_FREQ / hz;

	while (n > 0) {
		tick = gettick();
		if (tick > otick) {
			n -= limit - (tick - otick);
		} else {
			n -= otick - tick;
		}
		otick = tick;
	}
}

#if (NPCPPI > 0)
int
sysbeepmatch(parent, match, aux)
	struct device *parent;
	struct cfdata *match;
	void *aux;
{
	return (!ppi_attached);
}

void
sysbeepattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	printf("\n");

	ppicookie = ((struct pcppi_attach_args *)aux)->pa_cookie;
	ppi_attached = 1;
}
#endif

void
sysbeepstop(arg)
	void *arg;
{

	/* disable counter 2 */
	disable_intr();
	outb(PITAUX_PORT, inb(PITAUX_PORT) & ~PIT_SPKR);
	enable_intr();
	beeping = 0;
}

void
sysbeep(pitch, period)
	int pitch, period;
{
	static int last_pitch;

	if (beeping)
		untimeout(sysbeepstop, 0);
	if (pitch == 0 || period == 0) {
		sysbeepstop(0);
		last_pitch = 0;
		return;
	}
	if (!beeping || last_pitch != pitch) {
		disable_intr();
		outb(TIMER_MODE, TIMER_SEL2 | TIMER_16BIT | TIMER_SQWAVE);
		outb(TIMER_CNTR2, TIMER_DIV(pitch) % 256);
		outb(TIMER_CNTR2, TIMER_DIV(pitch) / 256);
		outb(PITAUX_PORT, inb(PITAUX_PORT) | PIT_SPKR);	/* enable counter 2 */
		enable_intr();
	}
	last_pitch = pitch;
	beeping = 1;
	timeout(sysbeepstop, 0, period);
}

unsigned int delaycount;	/* calibrated loop variable (1 millisecond) */

#define FIRST_GUESS   0x2000

void
findcpuspeed()
{
	int i;
	int remainder;

	/* Put counter in count down mode */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_16BIT | TIMER_RATEGEN);
	outb(TIMER_CNTR0, 0xff);
	outb(TIMER_CNTR0, 0xff);
	for (i = FIRST_GUESS; i; i--)
		;
	/* Read the value left in the counter */
	remainder = gettick();
	/*
	 * Formula for delaycount is:
	 *  (loopcount * timer clock speed) / (counter ticks * 1000)
	 */
	delaycount = (FIRST_GUESS * TIMER_DIV(1000)) / (0xffff-remainder);
}

void
i8254_initclocks()
{

	/*
	 * XXX If you're doing strange things with multiple clocks, you might
	 * want to keep track of clock handlers.
	 */
	(void)isa_intr_establish(NULL, 0, IST_PULSE, IPL_CLOCK, clockintr, 0);
}

void
rtcinit()
{
	static int first_rtcopen_ever = 1;

	if (!first_rtcopen_ever)
		return;
	first_rtcopen_ever = 0;

	mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_1024_Hz);	/* XXX softc */
	mc146818_write(NULL, MC_REGB, MC_REGB_24HR | MC_REGB_PIE);			/* XXX softc */
}

int
rtcget(regs)
	mc_todregs *regs;
{

	rtcinit();
	if ((mc146818_read(NULL, MC_REGD) & MC_REGD_VRT) == 0) /* XXX softc */
		return (-1);
	MC146818_GETTOD(NULL, regs);							/* XXX softc */
	return (0);
}	

void
rtcput(regs)
	mc_todregs *regs;
{
	rtcinit();
	MC146818_PUTTOD(NULL, regs);			/* XXX softc */
}

static int month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int
yeartoday(year)
	int year;
{
	return ((year % 4) ? 365 : 366);
}

int
bcdtobin(n)
	int n;
{
	return (((n >> 4) & 0x0f) * 10 + (n & 0x0f));
}

int
bintobcd(n)
	int n;
{
	return ((u_char)(((n / 10) << 4) & 0xf0) | ((n % 10) & 0x0f));
}

static int timeset;

/*
 * Initialize the time of day register, based on the time base which is, e.g.
 * from a filesystem.
 */
void
inittodr(base)
	time_t base;
{
	mc_todregs rtclk;
	time_t n;
	int sec, min, hr, dom, mon, yr, century, tcentury;
	int i, days = 0;
	int s;

	/*
	 * We mostly ignore the suggested time and go for the RTC clock time
	 * stored in the CMOS RAM.  If the time can't be obtained from the
	 * CMOS, or if the time obtained from the CMOS is 5 or more years
	 * less than the suggested time, we used the suggested time.  (In
	 * the latter case, it's likely that the CMOS battery has died.)
	 */

	if (base < 25*SECYR) {	/* if before 1995, something's odd... */
		printf("WARNING: preposterous time in file system\n");
		/* read the system clock anyway */
		base = 27*SECYR + 186*SECDAY + SECDAY/2;
	}

	s = splclock();
	if (rtcget(&rtclk)) {
		splx(s);
		printf("WARNING: invalid time in clock chip\n");
		goto fstime;
	}
	century = mc146818_read(NULL, RTC_CENTURY); /* XXX softc */
	splx(s);

	sec = bcdtobin(rtclk[MC_SEC]);
	min = bcdtobin(rtclk[MC_MIN]);
	hr = bcdtobin(rtclk[MC_HOUR]);
	dom = bcdtobin(rtclk[MC_DOM]);
	mon = bcdtobin(rtclk[MC_MONTH]);
	yr = bcdtobin(rtclk[MC_YEAR]);
	century = bcdtobin(century);
	tcentury = (yr < 70) ? 20 : 19;
	if (century != tcentury) {
		/* XXX note: saying "century is 20" might confuse the naive. */
		printf("WARNING: NVRAM century is %d but RTC year is %d\n",
		    century, yr);

		/* Kludge to roll over century. */
		if ((century == 19) && (tcentury == 20) && (yr == 00)) {
			printf("WARNING: Setting NVRAM century to 20\n");
			s = splclock();
			/* note: 0x20 = 20 in BCD. */
			mc146818_write(NULL, RTC_CENTURY, 0x20); /*XXXsoftc*/
			splx(s);
		} else {
			printf("WARNING: CHECK AND RESET THE DATE!\n");
		}
	}
	yr = (tcentury == 20) ? yr+100 : yr;
 
	/*
	 * If time_t is 32 bits, then the "End of Time" is 
	 * Mon Jan 18 22:14:07 2038 (US/Eastern)
	 * This code copes with RTC's past the end of time if time_t
	 * is an int32 or less. Needed because sometimes RTCs screw
	 * up or are badly set, and that would cause the time to go
	 * negative in the calculation below, which causes Very Bad
	 * Mojo. This at least lets the user boot and fix the problem.
	 * Note the code is self eliminating once time_t goes to 64 bits.
	 */
	if (sizeof(time_t) <= sizeof(int32_t)) {
		if (yr >= 138) {
			printf("WARNING: RTC time at or beyond 2038.\n");
			yr = 137;
			printf("WARNING: year set back to 2037.\n");
			printf("WARNING: CHECK AND RESET THE DATE!\n");
		}
	}

	n = sec + 60 * min + 3600 * hr;
	n += (dom - 1) * 3600 * 24;

	if (yeartoday(yr) == 366)
		month[1] = 29;
	for (i = mon - 2; i >= 0; i--)
		days += month[i];
	month[1] = 28;
	for (i = 70; i < yr; i++)
		days += yeartoday(i);
	n += days * 3600 * 24;

	n += rtc_offset * 60;

	if (base < n - 5*SECYR)
		printf("WARNING: file system time much less than clock time\n");
	else if (base > n + 5*SECYR) {
		printf("WARNING: clock time much less than file system time\n");
		printf("WARNING: using file system time\n");
		goto fstime;
	}

	timeset = 1;
	time.tv_sec = n;
	time.tv_usec = 0;
	return;

fstime:
	timeset = 1;
	time.tv_sec = base;
	time.tv_usec = 0;
	printf("WARNING: CHECK AND RESET THE DATE!\n");
}

/*
 * Reset the clock.
 */
void
resettodr()
{
	mc_todregs rtclk;
	time_t n;
	int diff, i, j, century;
	int s;

	/*
	 * We might have been called by boot() due to a crash early
	 * on.  Don't reset the clock chip in this case.
	 */
	if (!timeset)
		return;

	s = splclock();
	if (rtcget(&rtclk))
		bzero(&rtclk, sizeof(rtclk));
	splx(s);

	diff = rtc_offset * 60;
	n = (time.tv_sec - diff) % (3600 * 24);   /* hrs+mins+secs */
	rtclk[MC_SEC] = bintobcd(n % 60);
	n /= 60;
	rtclk[MC_MIN] = bintobcd(n % 60);
	rtclk[MC_HOUR] = bintobcd(n / 60);

	n = (time.tv_sec - diff) / (3600 * 24);	/* days */
	rtclk[MC_DOW] = (n + 4) % 7;  /* 1/1/70 is Thursday */

	for (j = 1970, i = yeartoday(j); n >= i; j++, i = yeartoday(j))
		n -= i;

	rtclk[MC_YEAR] = bintobcd((j - 1900)%100);
	century = bintobcd(j/100);

	if (i == 366)
		month[1] = 29;
	for (i = 0; n >= month[i]; i++)
		n -= month[i];
	month[1] = 28;
	rtclk[MC_MONTH] = bintobcd(++i);

	rtclk[MC_DOM] = bintobcd(++n);

	s = splclock();
	rtcput(&rtclk);
	mc146818_write(NULL, RTC_CENTURY, century); /* XXX softc */
	splx(s);
}

void
setstatclockrate(arg)
	int arg;
{
	if (arg == stathz) {
		mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_128_Hz);
	} else {
		mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_1024_Hz);
	}
}
