/*
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code contains ideas from software contributed to Berkeley by
 * Avadis Tevanian, Jr., Michael Wayne Young, and the Mach Operating
 * System project at Carnegie-Mellon University.
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
 *	@(#)kern_lock.c	8.18 (Berkeley) 5/21/95
 */

/* To become kern_lock.c: after rwlock & mutex are fully implemented */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <machine/cpu.h>

#include "devel/sys/lockmgr.h"

/* Initialize a lock; required before use. */
void
lock_init(lkp, prio, wmesg, timo, flags)
	struct lockmgr *lkp;
	char *wmesg;
	int prio, timo;
	unsigned int flags;
{
	bzero(lkp, sizeof(struct lock));
	simple_lock_init(&lkp->lk_interlock);
	lkp->lk_lock = 0;
	lkp->lk_flags = flags & LK_EXTFLG_MASK;
	lkp->lk_prio = prio;
	lkp->lk_timo = timo;
	lkp->lk_wmesg = wmesg;
	lkp->lk_lockholder = LK_NOPROC;
}

int lock_wait_time = 100;
void
pause(lkp, wanted)
	struct lock *lkp;
	int wanted;
{
	if (lock_wait_time > 0) {
		int i;
		simple_unlock(&lkp);
		for(i = lock_wait_time; i > 0; i--) {
			if (!(wanted)) {
				break;
			}
		}
		simple_lock(&lkp);
	}
	if (!(wanted)) {
		break;
	}
}

void
acquire(lkp, error, extflags, wanted)
	struct lock *lkp;
	int error, extflags, wanted;
{
	pause(lkp, wanted);
	for (error = 0; wanted; ) {
		lkp->lk_waitcount++;
		simple_unlock(&lkp);
		error = tsleep((void *)lkp, lkp->lk_prio, lkp->lk_wmesg, lkp->lk_timo);
		simple_lock(&lkp);
		lkp->lk_waitcount--;
		if (error) {
			break;
		}
		if ((extflags) & LK_SLEEPFAIL) {
			error = ENOLCK;
			break;
		}
	}
}

void
count(p, x)
	struct proc p;
	short x;
{
	if(p) {
		p->p_locks += x;
	}
}

#if defined(DEBUG) && NCPUS == 1
#include <sys/kernel.h>
#include <vm/include/vm.h>
#include <sys/sysctl.h>

int lockpausetime = 0;
struct ctldebug debug2 = { "lockpausetime", &lockpausetime };
int simplelockrecurse;

/*
 * Simple lock functions so that the debugger can see from whence
 * they are being called.
 */
void
simple_lock_init(alp)
	struct simplelock *alp;
{
	alp->lock_data = 0;
}

void
_simple_lock(alp, id, l)
	__volatile struct simplelock *alp;
	const char *id;
	int l;
{

	if (simplelockrecurse)
		return;
	if (alp->lock_data == 1) {
		if (lockpausetime == -1)
			panic("%s:%d: simple_lock: lock held", id, l);
		printf("%s:%d: simple_lock: lock held\n", id, l);
		if (lockpausetime == 1) {
			BACKTRACE(curproc);
		} else if (lockpausetime > 1) {
			printf("%s:%d: simple_lock: lock held...", id, l);
			tsleep(&lockpausetime, PCATCH | PPAUSE, "slock",
			    lockpausetime * hz);
			printf(" continuing\n");
		}
	}
	alp->lock_data = 1;
	if (curproc)
		curproc->p_simple_locks++;
}

int
_simple_lock_try(alp, id, l)
	__volatile struct simplelock *alp;
	const char *id;
	int l;
{

	if (alp->lock_data)
		return (0);
	if (simplelockrecurse)
		return (1);
	alp->lock_data = 1;
	if (curproc)
		curproc->p_simple_locks++;
	return (1);
}

void
_simple_unlock(alp, id, l)
	__volatile struct simplelock *alp;
	const char *id;
	int l;
{

	if (simplelockrecurse)
		return;
	if (alp->lock_data == 0) {
		if (lockpausetime == -1)
			panic("%s:%d: simple_unlock: lock not held", id, l);
		printf("%s:%d: simple_unlock: lock not held\n", id, l);
		if (lockpausetime == 1) {
			BACKTRACE(curproc);
		} else if (lockpausetime > 1) {
			printf("%s:%d: simple_unlock: lock not held...", id, l);
			tsleep(&lockpausetime, PCATCH | PPAUSE, "sunlock",
			    lockpausetime * hz);
			printf(" continuing\n");
		}
	}
	alp->lock_data = 0;
	if (curproc)
		curproc->p_simple_locks--;
}
#endif /* DEBUG && NCPUS == 1 */
