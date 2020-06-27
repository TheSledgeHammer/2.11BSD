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
 *	Reworked from: @(#)lock.h	8.12 (Berkeley) 5/19/95
 */

#ifndef SYS_MUTEX_H_
#define SYS_MUTEX_H_

#include "lock.h"
#include "sys/tcb.h"

struct mutex {
    volatile unsigned int   mtx_lock;

    struct kthread          *mtx_ktlockholder; 	/* Kernel Thread lock holder */
    struct uthread          *mtx_utlockholder;	/* User Thread lock holder */

    struct simplelock 	    *mtx_interlock; 	/* lock on remaining fields */
    int					    mtx_sharecount;		/* # of accepted shared locks */
    int					    mtx_waitcount;		/* # of processes sleeping for lock */
    short				    mtx_exclusivecount;	/* # of recursive exclusive locks */

    unsigned int		    mtx_flags;			/* see below */
    short				    mtx_prio;			/* priority at which to sleep */
    char				    *mtx_wmesg;			/* resource sleeping (for tsleep) */
    int					    mtx_timo;			/* maximum sleep time (for tsleep) */
    tid_t                   mtx_lockholder;

    struct lockmgr			*mtx_lockp;			/* pointer to struct lockmgr */
};

#define MTX_THREAD  		((tid_t) -2)
#define MTX_NOTHREAD    	((tid_t) -1)

/* These are flags that are passed to the lockmgr routine. */
#define MTX_TYPE_MASK	    0x0FFFFFFF
#define MTX_SHARED	        0x00000001	/* shared lock */
#define MTX_EXCLUSIVE	    0x00000002
#define MTX_UPGRADE	        0x00000003	/* shared-to-exclusive upgrade */
#define MTX_EXCLUPGRADE	    0x00000004	/* first shared-to-exclusive upgrade */
#define MTX_DOWNGRADE	    0x00000005	/* exclusive-to-shared downgrade */
#define MTX_RELEASE  	    0x00000006	/* release any type of lock */
#define MTX_DRAIN	        0x00000007	/* wait for all lock activity to end */

/* External lock flags. */
#define MTX_EXTFLG_MASK	    0x00000070	/* mask of external flags */
#define MTX_NOWAIT	        0x00000010	/* do not sleep to await lock */
#define MTX_SLEEPFAIL	    0x00000020	/* sleep, then return failure */
#define MTX_CANRECURSE	    0x00000040	/* allow recursive exclusive lock */
#define MTX_REENABLE	    0x00000080	/* lock is be reenabled after drain */

/* Internal lock flags. */
#define MTX_WANT_UPGRADE	0x00000100	/* waiting for share-to-excl upgrade */
#define MTX_WANT_EXCL	    0x00000200	/* exclusive lock sought */
#define MTX_HAVE_EXCL	    0x00000400	/* exclusive lock obtained */
#define MTX_WAITDRAIN	    0x00000800	/* process waiting for lock to drain */
#define MTX_DRAINING	    0x00004000	/* lock is being drained */
#define MTX_DRAINED	        0x00008000	/* lock has been decommissioned */

/* Control flags. */
#define MTX_INTERLOCK	    0x00010000	/* unlock passed simple lock after getting lk_interlock */
#define MTX_RETRY			0x00020000	/* vn_lock: retry until locked */

/* Generic Mutex Functions */
void mutex_init(mutex_t, int, char *, int, unsigned int);
int mutex_lock(__volatile mutex_t);
int mutex_lock_try(__volatile mutex_t);
int mutex_timedlock(__volatile mutex_t);
int mutex_unlock(__volatile mutex_t);
int mutex_destroy(__volatile mutex_t);

int mutexstatus(mutex_t);
int mutexmgr(__volatile mutex_t, unsigned int, tid_t);

#include <lock.h>

#if NCPUS > 1
#define PAUSE(mtx, wanted)						\
		pause((mtx)->mtx_lockp, wanted);
#else /* NCPUS == 1 */
#define PAUSE(mtx, wanted)
#endif /* NCPUS == 1 */

#define ACQUIRE(mtx, error, extflags, wanted)	\
		acquire((mtx)->mtx_lockp, error, extflags, wanted);

#ifdef DEBUG
#define COUNT(p, x) 							\
		count(p, x);
#else
#define COUNT(p, x)
#endif

#endif /* SYS_MUTEX_H_ */
