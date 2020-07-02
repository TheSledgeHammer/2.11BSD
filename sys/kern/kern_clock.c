/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)kern_clock.c	1.4 (2.11BSD GTE) 1997/2/14
 */

#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/callout.h>
#include <sys/dk.h>
#include <sys/kernel.h>
#include <sys/systm.h>
//#include <sys/resourcevar.h>
//#include <sys/signalvar.h>
#include <sys/time.h>
#include <sys/signal.h>

#include <machine/cpu.h>
#include <machine/clock.h>

#ifdef GPROF
#include <sys/gmon.h>
#endif

int noproc;
struct  callout *callfree, calltodo;

int	profprocs;
static int psdiv, pscnt;	/* prof => stat divider */

volatile struct timeval time;
volatile struct	timeval mono_time;

/*
 * Initialize clock frequencies and start both clocks running.
 */
void
initclocks()
{
	register int i;

	/*
	 * Set divisors to 1 (normal case) and let the machine-specific
	 * code do its bit.
	 */
	psdiv = pscnt = 1;
	cpu_initclocks();


	i = stathz ? stathz : hz;
	if (profhz == 0)
		profhz = i;
	psratio = profhz / i;

}
/*
 * The hz hardware interval timer.
 * We update the events relating to real time.
 * Also gather statistics.
 *
 *	reprime clock
 *	implement callouts
 *	maintain user/system times
 *	maintain date
 *	profile
 */
/*ARGSUSED*/
void
hardclock(dev, sp, r1, ov, nps, r0, pc, ps)
	dev_t dev;
	caddr_t sp, pc;
	int r1, ov, nps, r0, ps;
{
	register struct callout *p1;
	register struct proc *p;
	register int needsoft = 0;

	/*
	 * Update real-time timeout queue.
	 * At front of queue are some number of events which are ``due''.
	 * The time to these is <= 0 and if negative represents the
	 * number of ticks which have passed since it was supposed to happen.
	 * The rest of the q elements (times > 0) are events yet to happen,
	 * where the time for each is given as a delta from the previous.
	 * Decrementing just the first of these serves to decrement the time
	 * to all events.
	 */
	p1 = calltodo.c_next;
	while (p1) {
		if (--p1->c_time > 0)
			break;
		needsoft = 1;
		if (p1->c_time == 0)
			break;
		p1 = p1->c_next;
	}

	/*
	 * Charge the time out based on the mode the cpu is in.
	 * Here again we fudge for the lack of proper interval timers
	 * assuming that the current state has been around at least
	 * one tick.
	 */
	if (USERMODE(ps)) {
		if (u->u_prof.pr_scale)
			needsoft = 1;
		/*
		 * CPU was in user state.  Increment
		 * user time counter, and process process-virtual time
		 * interval timer. 
		 */
		u->u_ru.ru_utime++;
		if (u->u_timer[ITIMER_VIRTUAL - 1].it_value &&
		    !--u->u_timer[ITIMER_VIRTUAL - 1].it_value) {
			psignal(u->u_procp, SIGVTALRM);
			u->u_timer[ITIMER_VIRTUAL - 1].it_value =
			    u->u_timer[ITIMER_VIRTUAL - 1].it_interval;
		}
	} else {
		/*
		 * CPU was in system state.
		 */
		if (!noproc)
			u->u_ru.ru_stime++;
	}

	/*
	 * If the cpu is currently scheduled to a process, then
	 * charge it with resource utilization for a tick, updating
	 * statistics which run in (user+system) virtual time,
	 * such as the cpu time limit and profiling timers.
	 * This assumes that the current process has been running
	 * the entire last tick.
	 */
	if (noproc == 0) {
		p = u->u_procp;
		if (++p->p_cpu == 0)
			p->p_cpu--;
		if ((u->u_ru.ru_utime+u->u_ru.ru_stime+1) >
		    u->u_rlimit[RLIMIT_CPU].rlim_cur) {
			psignal(p, SIGXCPU);
			if (u->u_rlimit[RLIMIT_CPU].rlim_cur <
			    u->u_rlimit[RLIMIT_CPU].rlim_max)
				u->u_rlimit[RLIMIT_CPU].rlim_cur += 5 * hz;
		}
		if (u->u_timer[ITIMER_PROF - 1].it_value &&
		    !--u->u_timer[ITIMER_PROF - 1].it_value) {
			psignal(p, SIGPROF);
			u->u_timer[ITIMER_PROF - 1].it_value =
			    u->u_timer[ITIMER_PROF - 1].it_interval;
		}
	}


	gatherstats(pc,ps);

	/*
	 * Increment the time-of-day, process callouts at a very
	 * low cpu priority, so we don't keep the relatively  high
	 * clock interrupt priority any longer than necessary.
	 */
	if (adjdelta) 
		if (adjdelta > 0) {
			++lbolt;
			--adjdelta;
		} else {
			--lbolt;
			++adjdelta;
		}
	if (++lbolt >= hz) {
		lbolt -= hz;
		++time.tv_sec;
	}

	if (needsoft && BASEPRI(ps)) {	/* if ps is high, just return */
		(void) splsoftclock();
		softclock(pc,ps);
	}
}

int	dk_ndrive = DK_NDRIVE;

/*
 * Gather statistics on resource utilization.
 *
 * We make a gross assumption: that the system has been in the
 * state it is in (user state, kernel state, interrupt state,
 * or idle state) for the entire last time interval, and
 * update statistics accordingly.
 */
/*ARGSUSED*/
void
gatherstats(pc, ps)
	caddr_t pc;
	int ps;
{
	register int cpstate, s;

	/*
	 * Determine what state the cpu is in.
	 */
	if (USERMODE(ps)) {
		/*
		 * CPU was in user state.
		 */
		if (u->u_procp->p_nice > NZERO)
			cpstate = CP_NICE;
		else
			cpstate = CP_USER;
	} else {
		/*
		 * CPU was in system state.  If profiling kernel
		 * increment a counter.  If no process is running
		 * then this is a system tick if we were running
		 * at a non-zero IPL (in a driver).  If a process is running,
		 * then we charge it with system time even if we were
		 * at a non-zero IPL, since the system often runs
		 * this way during processing of system calls.
		 * This is approximate, but the lack of true interval
		 * timers makes doing anything else difficult.
		 */
		cpstate = CP_SYS;
		if (noproc && BASEPRI(ps))
			cpstate = CP_IDLE;
	}
	/*
	 * We maintain statistics shown by user-level statistics
	 * programs:  the amount of time in each cpu state, and
	 * the amount of time each of DK_NDRIVE ``drives'' is busy.
	 */
	cp_time[cpstate]++;
	for (s = 0; s < DK_NDRIVE; s++)
		if (dk_busy & (1 << s))
			dk_time[s]++;
}


/*
 * Software priority level clock interrupt.
 * Run periodic events from timeout queue.
 */
void
softclock(pc, ps)
	caddr_t pc;
	int ps;
{
	for (;;) {
		register struct callout *p1;
		register caddr_t arg;
		register int (*func)();
		register int a, s;

		s = splhigh();
		if ((p1 = calltodo.c_next) == 0 || p1->c_time > 0) {
			splx(s);
			break;
		}
		arg = p1->c_arg; func = p1->c_func; a = p1->c_time;
		calltodo.c_next = p1->c_next;
		p1->c_next = callfree;
		callfree = p1;
		splx(s);
#ifdef INET
		if (ISSUPERADD(func))
			KScall(KERNELADD(func), sizeof(arg) + sizeof(a),
			    arg, a);
		else
#endif
		(*func)(arg, a);
	}
	/*
	 * If trapped user-mode and profiling, give it
	 * a profiling tick.
	 */
	if (USERMODE(ps)) {
		register struct proc *p = u->u_procp;

		if (u->u_prof.pr_scale)
			addupc(pc, &u->u_prof, 1);
		/*
		 * Check to see if process has accumulated
		 * more than 10 minutes of user time.  If so
		 * reduce priority to give others a chance.
		 */

		if (p->p_uid && p->p_nice == NZERO &&
		    u->u_ru.ru_utime > 10L * 60L * hz) {
			p->p_nice = NZERO+4;
				(void) setpri(p);
		}
	}
}

/*
 * Arrange that (*fun)(arg) is called in t/hz seconds.
 */
void
timeout(fun, arg, t)
	int (*fun)();
	caddr_t arg;
	register int t;
{
	register struct callout *p1, *p2, *pnew;
	register int s = splclock();

	if (t <= 0)
		t = 1;
	pnew = callfree;
	if (pnew == NULL)
		panic("timeout table overflow");
	callfree = pnew->c_next;
	pnew->c_arg = arg;
	pnew->c_func = fun;
	for (p1 = &calltodo; (p2 = p1->c_next) && p2->c_time < t; p1 = p2)
		if (p2->c_time > 0)
			t -= p2->c_time;
	p1->c_next = pnew;
	pnew->c_next = p2;
	pnew->c_time = t;
	if (p2)
		p2->c_time -= t;
	splx(s);
}

/*
 * untimeout is called to remove a function timeout call
 * from the callout structure.
 */
void
untimeout(fun, arg)
	int (*fun)();
	caddr_t arg;
{
	register struct callout *p1, *p2;
	register int s;

	s = splclock();
	for (p1 = &calltodo; (p2 = p1->c_next) != 0; p1 = p2) {
		if (p2->c_func == fun && p2->c_arg == arg) {
			if (p2->c_next && p2->c_time > 0)
				p2->c_next->c_time += p2->c_time;
			p1->c_next = p2->c_next;
			p2->c_next = callfree;
			callfree = p2;
			break;
		}
	}
	splx(s);
}

void
profil()
{
	register struct a {
		short	*bufbase;
		unsigned bufsize;
		unsigned pcoffset;
		unsigned pcscale;
	} *uap = (struct a *)u->u_ap;
	register struct uprof *upp = &u->u_prof;

	upp->pr_base = uap->bufbase;
	upp->pr_size = uap->bufsize;
	upp->pr_off = uap->pcoffset;
	upp->pr_scale = uap->pcscale;
}

/*
 * Compute number of hz until specified time.
 * Used to compute third argument to timeout() from an
 * absolute time.
 */
int
hzto(tv)
	register struct timeval *tv;
{
	register long ticks;
	register long sec;
	register int s = splhigh();

	/*
	 * If number of milliseconds will fit in 32 bit arithmetic,
	 * then compute number of milliseconds to time and scale to
	 * ticks.  Otherwise just compute number of hz in time, rounding
	 * times greater than representible to maximum value.
	 *
	 * Delta times less than 25 days can be computed ``exactly''.
	 * Maximum value for any timeout in 10ms ticks is 250 days.
	 */

	sec = tv->tv_sec - time->tv_sec;
	if (sec <= 0x7fffffff / 1000 - 1000)
		ticks = ((tv->tv_sec - time.tv_sec) * 1000 +
			(tv->tv_usec - time.tv_usec) / 1000) / (1000/hz);
	else if (sec <= 0x7fffffff / hz)
		ticks = sec * hz;
	else
		ticks = 0x7fffffff;
	splx(s);
	return ((int)ticks);
}

/*
 * Start profiling on a process.
 *
 * Kernel profiling passes proc0 which never exits and hence
 * keeps the profile clock running constantly.
 */
void
startprofclock(p)
	register struct proc *p;
{
	int s;

	if ((p->p_flag & P_PROFIL) == 0) {
		p->p_flag |= P_PROFIL;
		if (++profprocs == 1 && stathz != 0) {
			s = splstatclock();
			psdiv = pscnt = psratio;
			setstatclockrate(profhz);
			splx(s);
		}
	}
}

/*
 * Stop profiling on a process.
 */
void
stopprofclock(p)
	register struct proc *p;
{
	int s;

	if (p->p_flag & P_PROFIL) {
		p->p_flag &= ~P_PROFIL;
		if (--profprocs == 0 && stathz != 0) {
			s = splstatclock();
			psdiv = pscnt = 1;
			setstatclockrate(stathz);
			splx(s);
		}
	}
}

/*
 * Return information about system clocks.
 */
int
sysctl_clockrate(where, sizep)
	register char *where;
	size_t *sizep;
{
	struct clockinfo clkinfo;

	/* Construct clockinfo structure. */
	clkinfo.hz = hz;
	clkinfo.tick = tick;
	clkinfo.profhz = profhz;
	clkinfo.stathz = stathz ? stathz : hz;
	return (sysctl_rdstruct(where, sizep, NULL, &clkinfo, sizeof(clkinfo)));
}
