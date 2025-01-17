/*	$NetBSD: mbuf.h,v 1.90.2.4 2004/09/11 18:08:57 he Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1999, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center and Matt Thomas of 3am Software Foundry.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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

/*
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)mbuf.h	8.5 (Berkeley) 2/19/95
 */
/*
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 *
 *	@(#)mbuf.h	7.8.2 (2.11BSD GTE) 12/31/93
 */

#ifndef	_SYS_MBUF_H_
#define	_SYS_MBUF_H_

#ifndef M_WAITOK
#include <sys/malloc.h>
#endif

/*
 * The default values for NMBUFS and NMBCLUSTERS (160 and 12 respectively)
 * result in approximately 32K bytes of buffer memory being allocated to
 * the network.  Taking into account the other data used by the network,
 * this leaves approximately 8K bytes free, so the formula is roughly:
 *
 * (NMBUFS / 8) + NMBCLUSTERS < 40
 */
#define	NMBUFS		170			/* number of mbufs */
#define	MSIZE		128			/* size of an mbuf */

#if CLBYTES > 1024
#define	MCLBYTES	1024
#define	MCLSHIFT	10
#define	MCLOFSET	(MCLBYTES - 1)
#else
#define	MCLBYTES	CLBYTES
#define	MCLSHIFT	CLSHIFT
#define	MCLOFSET	CLOFSET
#endif

#define	MMINOFF		8								/* mbuf header length */
#define	MTAIL		2
#define	MMAXOFF		(MSIZE-MTAIL)					/* offset where data ends */
#define	MLEN		(MSIZE-MMINOFF-MTAIL)			/* mbuf data length */
#define	MHLEN		(MLEN - sizeof(struct pkthdr))	/* data len w/pkthdr */
#define	NMBPCL		(CLBYTES/MSIZE)					/* # mbufs per cluster */
#define	MINCLSIZE	(MHLEN + MLEN)					/* smallest amount to put in cluster */

/* Packet tags structure */
struct m_tag {
	SLIST_ENTRY(m_tag)	m_tag_link;	/* List of packet tags */
	u_int16_t			m_tag_id;	/* Tag ID */
	u_int16_t			m_tag_len;	/* Length of data */
};

/*
 * Macros for type conversion
 */
/* network cluster number to virtual address, and back */
#define	cltom(x) 	((struct mbuf *)((int)mbutl + ((x) << MCLSHIFT)))
#define	mtocl(x) 	(((int)x - (int)mbutl) >> MCLSHIFT)

/* address in mbuf to mbuf head */
#define	dtom(x)		((struct mbuf *)((int)x & ~(MSIZE-1)))

/* mbuf head, to typed data */
#define	mtod(x,t)	((t)((int)(x) + (x)->m_off))

struct m_hdr {
	struct	mbuf 		*mh_next;		/* next buffer in chain */
	struct	mbuf 		*mh_nextpkt;	/* next chain in queue/record */
	struct	mbuf 		*mh_act;		/* link in higher-level mbuf list */
	u_short				mh_off;			/* offset of data */
	int					mh_len;			/* amount of data in this mbuf */
	short				mh_type;		/* mbuf type (0 == free) */
	caddr_t				mh_dat;			/* data storage */
	short				mh_flags;		/* flags; see below */
};

/* record/packet header in first mbuf of chain; valid if M_PKTHDR set */
struct	pkthdr {
	struct	ifnet 		*rcvif;			/* rcv interface */
	SLIST_HEAD(packet_tags, m_tag) tags;	/* list of packet tags */
	int					len;			/* total packet length */
	int					csum_flags;		/* checksum flags */
	u_int32_t 			csum_data;		/* checksum data */
};

/* description of external storage mapped into mbuf, valid if M_EXT set */
struct m_ext {
	caddr_t				ext_buf;		/* start of buffer */
	void				(*ext_free)();	/* free routine if not the usual */
	void				*ext_arg;		/* argument for ext_free */
	u_int				ext_size;		/* size of buffer, for ext_free */
	int					ext_type;		/* malloc type */
	struct mbuf 		*ext_nextref;
	struct mbuf 		*ext_prevref;
};

/* Contents of mbuf: */
struct mbuf {
	struct	m_hdr 				mbhdr;
	union {
		struct {
			struct pkthdr 		MH_pkthdr;	/* M_PKTHDR set */
			union {
				struct m_ext 	MH_ext;		/* M_EXT set */
				char			MH_databuf[MHLEN];
			} MH_dat;
		} MH;
		char					M_databuf[MLEN];	/* !M_PKTHDR, !M_EXT */
	} M_dat;
};

#define	m_next			mbhdr.mh_next
#define	m_len			mbhdr.mh_len
#define	m_data			mbhdr.mh_dat
#define	m_off			mbhdr.mh_off
#define	m_type			mbhdr.mh_type
#define	m_flags			mbhdr.mh_flags
#define	m_nextpkt		mbhdr.mh_nextpkt
#define	m_act			mbhdr.mh_act
#define	m_pkthdr		M_dat.MH.MH_pkthdr
#define	m_ext			M_dat.MH.MH_dat.MH_ext
#define	m_pktdat		M_dat.MH.MH_dat.MH_databuf
#define	m_dat			M_dat.M_databuf

/* mbuf flags */
#define	M_EXT		    0x00001		/* has associated external storage */
#define	M_PKTHDR	    0x00002		/* start of record */
#define	M_EOR		    0x00004		/* end of record */

/* mbuf pkthdr flags, also in m_flags */
#define M_AUTHIPHDR		0x00010		/* authenticated (IPsec) */
#define M_DECRYPTED		0x00020		/* decrypted (IPsec) */
#define M_LOOP		    0x00040		/* received on loopback */
#define M_AUTHIPDGM     0x0080              /* data origin authentication */
#define	M_BCAST		    0x00100		/* send/received as link-level broadcast */
#define	M_MCAST		    0x00200		/* send/received as link-level multicast */
#define	M_CANFASTFWD	0x00400		/* used by filters to indicate packet can be fast-forwarded */

#define M_ANYCAST6		0x00800		/* received as IPv6 anycast */
#define M_LINK0			0x01000		/* link layer specific flag */
#define M_LINK1			0x02000		/* link layer specific flag */
#define M_LINK2			0x04000		/* link layer specific flag */
#define M_LINK3			0x08000		/* link layer specific flag */
#define M_LINK4			0x10000		/* link layer specific flag */
#define M_LINK5			0x20000		/* link layer specific flag */
#define M_LINK6			0x40000		/* link layer specific flag */
#define M_LINK7			0x80000		/* link layer specific flag */

/* additional flags for M_EXT mbufs */
#define	M_EXT_CLUSTER	        0x01000000	/* ext is a cluster */

/* for source-level compatibility */
#define	M_CLUSTER	        M_EXT_CLUSTER

/* flags copied when copying m_pkthdr */
#define	M_COPYFLAGS		(M_PKTHDR|M_EOR|M_BCAST|M_MCAST|M_CANFASTFWD|M_ANYCAST6|M_LINK0|M_LINK1|M_LINK2|M_LOOP)

/* mbuf types */
#define	MT_FREE			M_FREE  	/* 0 should be on free list */
#define	MT_DATA			M_MBUF		/* 1 dynamic (data) allocation */
#define	MT_HEADER		M_MBUF		/* 2 packet header */
#define	MT_SOCKET		M_SOCKET	/* 3 socket structure */
#define	MT_PCB			M_PCB		/* 4 protocol control block */
#define	MT_RTABLE		M_RTABLE	/* 5 routing tables */
#define	MT_HTABLE		M_HTABLE	/* 6 IMP host tables */
#define	MT_ATABLE		0			/* 7 address resolution tables */
#define	MT_SONAME		M_MBUF		/* 8 socket name */
#define	MT_ZOMBIE		0			/* 9 zombie proc status */
#define	MT_SOOPTS		M_SOOPTS	/* 10 socket options */
#define	MT_FTABLE		M_FTABLE	/* 11 fragment reassembly header */
#define	MT_RIGHTS		M_MBUF 		/* 12 access rights */
#define	MT_IFADDR		M_IFADDR	/* 13 interface address */
#define MT_CONTROL		M_MBUF 		/* 14 extra-data protocol message */
#define MT_OOBDATA		M_MBUF		/* 15 expedited data  */
#define	NMBTYPES		16

/* flags to m_get */
#define	M_DONTWAIT		M_NOWAIT
#define	M_WAIT			M_WAITOK
#define	M_DONTWAITLONG	2

/* flags to m_pgalloc */
#define	MPG_MBUFS		0		/* put new mbufs on free list */
#define	MPG_CLUSTERS	1		/* put new clusters on free list */
#define	MPG_SPACE		2		/* don't free; caller wants space */

/* length to m_copy to copy all */
#define	M_COPYALL		1000000000

/* Checksumming flags (csum_flags). */
#define M_CSUM_TCPv4		0x00000001	/* TCP header/payload */
#define M_CSUM_UDPv4		0x00000002	/* UDP header/payload */
#define M_CSUM_TCP_UDP_BAD	0x00000004	/* TCP/UDP checksum bad */
#define M_CSUM_DATA			0x00000008	/* consult csum_data */
#define M_CSUM_TCPv6		0x00000010	/* IPv6 TCP header/payload */
#define M_CSUM_UDPv6		0x00000020	/* IPv6 UDP header/payload */
#define M_CSUM_IPv4			0x00000040	/* IPv4 header */
#define M_CSUM_IPv4_BAD		0x00000080	/* IPv4 header checksum bad */
#define M_CSUM_TSOv4		0x00000100	/* TCPv4 segmentation offload */
#define M_CSUM_TSOv6		0x00000200	/* TCPv6 segmentation offload */

/* Checksum-assist quirks: keep separate from jump-table bits. */
#define M_CSUM_BLANK		0x40000000	/* csum is missing */
#define M_CSUM_NO_PSEUDOHDR	0x80000000	/* Rx csum_data does not include
						 * the UDP/TCP pseudo-hdr, and
						 * is not yet 1s-complemented.
						 */

#define M_CSUM_BITS \
    "\20\1TCPv4\2UDPv4\3TCP_UDP_BAD\4DATA\5TCPv6\6UDPv6\7IPv4\10IPv4_BAD" \
    "\11TSOv4\12TSOv6\39BLANK\40NO_PSEUDOHDR"

/*
 * Macros for manipulating csum_data on outgoing packets. These are
 * used to pass information down from the L4/L3 to the L2.
 *
 *   _IPHL:   Length of the IPv{4/6} header, plus the options; in other
 *            words the offset of the UDP/TCP header in the packet.
 *   _OFFSET: Offset of the checksum field in the UDP/TCP header.
 */
#define M_CSUM_DATA_IPv4_IPHL(x)	((x) >> 16)
#define M_CSUM_DATA_IPv4_OFFSET(x)	((x) & 0xffff)
#define M_CSUM_DATA_IPv6_IPHL(x)	((x) >> 16)
#define M_CSUM_DATA_IPv6_OFFSET(x)	((x) & 0xffff)
#define M_CSUM_DATA_IPv6_SET(x, v)	(x) = ((x) & 0xffff) | ((v) << 16)

/*
 * mbuf utility macros:
 */

/*
 * m_pullup will pull up additional length if convenient;
 * should be enough to hold headers of second-level and higher protocols. 
 */
#define	MPULL_EXTRA	32
#define	MGET(m, i, t) {									\
	int ms = splimp(); 									\
	MALLOC((m), struct mbuf *, MSIZE, t, (i)); 			\
	if ((m) == mbfree) {								\
		if ((m)->m_type != MT_FREE) { 					\
			panic("mget");								\
		}												\
		(m)->m_type = t;								\
		mbstat.m_mtypes[MT_FREE]--;			            \
		mbstat.m_mtypes[t]++;				            \
		mbfree = (m)->m_next; 							\
		(m)->m_next = (struct mbuf *)NULL; 				\
		(m)->m_nextpkt = (struct mbuf *)NULL; 			\
		(m)->m_data = (m)->m_dat; 						\
		(m)->m_flags = 0; 								\
		(m)->m_off = MMINOFF; 							\
	} else {											\
		(m) = m_retry((i), (t)); 						\
	} 													\
	splx(ms); 											\
}

#define	MGETHDR(m, i, t) { 								\
	int ms = splimp(); 									\
	MALLOC((m), struct mbuf *, MSIZE, t, (i)); 			\
	if ((m) == mbfree) { 								\
		(m)->m_type = (t); 								\
		mbstat.m_mtypes[MT_FREE]--;			            \
		mbstat.m_mtypes[t]++; 				            \
		mbfree = (m)->m_next; 							\
		(m)->m_next = (struct mbuf *)NULL; 				\
		(m)->m_nextpkt = (struct mbuf *)NULL; 			\
		(m)->m_data = (m)->m_pktdat; 					\
		(m)->m_flags = M_PKTHDR; 						\
	} else { 											\
		(m) = m_retryhdr((i), (t)); 					\
	}													\
	splx(ms); 											\
}

#define	MCLISREFERENCED(m)	((m)->m_ext.ext_nextref != (m))

/*
 * Mbuf page cluster macros.
 * MCLALLOC allocates mbuf page clusters.
 * Note that it works only with a count of 1 at the moment.
 * MCLGET adds such clusters to a normal mbuf.
 * m->m_len is set to MCLBYTES upon success, and to MLEN on failure.
 * MCLFREE frees clusters allocated by MCLALLOC.
 */

union mcluster {
	union	mcluster *mcl_next;
	char	mcl_buf[MCLBYTES];
};

#define	MCLALLOC(m, i) {								\
	  if (mclfree == 0) {								\
		(void)m_clalloc(1, (i));                        \
	  } 												\
	  if (((m) = (caddr_t)mclfree) != 0) { 				\
	     ++mclrefcnt[mtocl(m)];							\
	     mbstat.m_clfree--;								\
	     mclfree = ((union mcluster *)(m))->mcl_next;	\
	  } 												\
}

#define	M_HASCL(m)	((m)->m_off >= MSIZE)
#define	MTOCL(m)	((struct mbuf *)(mtod((m), int) &~ MCLOFSET))

#define	MCLGET(m, i) { 									\
	  MCLALLOC((m)->m_ext.ext_buf, i);					\
	  if ((m)->m_ext.ext_buf != NULL) { 				\
		  (m)->m_data = (m)->m_ext.ext_buf; 			\
		  (m)->m_flags |= M_EXT|M_CLUSTER;	\
		  (m)->m_ext.ext_size = MCLBYTES;  				\
	  }													\
}

#define	MCLFREE(m) { 									\
	if (--mclrefcnt[mtocl(m)] == 0) { 					\
		((union mcluster *)(m))->mcl_next = mclfree; 	\
		mclfree = (union mcluster *)(m);				\
	    mbstat.m_clfree++;								\
	}													\
}

#define	MFREE(m, n) {									\
	int ms = splimp(); 									\
	if ((m)->m_type == MT_FREE) {						\
		panic("mfree");									\
	}													\
	mbstat.m_mtypes[(m)->m_type]--;			            \
	mbstat.m_mtypes[MT_FREE]++; 				        \
	(m)->m_type = MT_FREE; 								\
	if (M_HASCL(m)) { 									\
		(n) = MTOCL(m); 								\
		MCLFREE(n); 									\
	} 													\
	(n) = (m)->m_next;									\
	(m)->m_next = mbfree; 								\
	(m)->m_off = 0; 									\
	(m)->m_act = 0; 									\
	mbfree = (m); 										\
	if((m)->m_flags & M_EXT) {							\
		if ((m)->m_ext.ext_free) {						\
			(*((m)->m_ext.ext_free))((m)->m_ext.ext_buf, \
					(m)->m_ext.ext_size); 				\
		} else {										\
			MCLFREE((m)->m_ext.ext_buf); 				\
		}												\
	}													\
	(n) = (m)->m_next; 									\
	FREE((m), (m)->m_type); 							\
	if (m_want) { 										\
		m_want = 0; 									\
		wakeup((caddr_t)&mbfree); 						\
	} 													\
	splx(ms); 											\
}

/*
 * Set the m_data pointer of a newly-allocated mbuf (m_get/MGET) to place
 * an object of the specified size at the end of the mbuf, longword aligned.
 */
#define	M_ALIGN(m, len) 								\
	{ (m)->m_data += (MLEN - (len)) &~ (sizeof(long) - 1); }
/*
 * As above, for mbufs allocated with m_gethdr/MGETHDR
 * or initialized by M_COPY_PKTHDR.
 */
#define	MH_ALIGN(m, len) 								\
	{ (m)->m_data += (MHLEN - (len)) &~ (sizeof(long) - 1); }

/*
 * Determine if an mbuf's data area is read-only.  This is true
 * for non-cluster external storage and for clusters that are
 * being referenced by more than one mbuf.
 */
#define	M_READONLY(m)									\
	(((m)->m_flags & M_EXT) != 0 &&						\
	  (((m)->m_flags & M_CLUSTER) == 0 /*|| MCLISREFERENCED(m)*/))

/*
 * Compute the amount of space available
 * before the current start of data in an mbuf.
 */
#define	M_LEADINGSPACE(m) 								\
	((m)->m_flags & M_EXT ? /* (m)->m_data - (m)->m_ext.ext_buf */ 0 : \
	    (m)->m_flags & M_PKTHDR ? (m)->m_data - (m)->m_pktdat : \
	    (m)->m_data - (m)->m_dat)

/*
 * Compute the amount of space available
 * after the end of data in an mbuf.
 */
#define	M_TRAILINGSPACE(m) 								\
	((m)->m_flags & M_EXT ? (m)->m_ext.ext_buf + (m)->m_ext.ext_size - \
	    ((m)->m_data + (m)->m_len) : 					\
	    &(m)->m_dat[MLEN] - ((m)->m_data + (m)->m_len))

/*
 * Arrange to prepend space of size plen to mbuf m.
 * If a new mbuf must be allocated, how specifies whether to wait.
 * If how is M_DONTWAIT and allocation fails, the original mbuf chain
 * is freed and m is set to NULL.
 */
#define	M_PREPEND(m, plen, how) { 						\
	if (M_LEADINGSPACE(m) >= (plen)) { 					\
		(m)->m_data -= (plen); 							\
		(m)->m_len += (plen); 							\
	} else 												\
		(m) = m_prepend((m), (plen), (how)); 			\
	if ((m) && (m)->m_flags & M_PKTHDR) 				\
		(m)->m_pkthdr.len += (plen); 					\
}

/* change mbuf to new type */
#define MCHTYPE(m, t) { 								\
	mbstat.m_mtypes[(m)->m_type]--;                     \
	mbstat.m_mtypes[t]++;                               \
	(m)->m_type = t;									\
}

/*
 * Mbuf statistics.
 */
struct mbstat {
	u_short	m_mbufs;			/* mbufs obtained from page pool */
	u_short	m_clusters;			/* clusters obtained from page pool */
	u_short	m_space;			/* interface pages obtained from page pool */
	u_short	m_clfree;			/* free clusters */
	u_short	m_drops;			/* times failed to find space */
	u_short m_wait;				/* times waited for space */
	u_short m_drain;			/* times drained protocols for space */
	u_short	m_mtypes[256];		/* type specific mbuf allocations */
};

#ifdef	_KERNEL
struct ifnet;

extern struct mbuf *mbutl;		/* virtual address of net free mem */
extern struct mbuf *mbfree;
extern struct mbstat mbstat;
extern union mcluster *mclfree;
extern int nmbclusters;
extern char *mclrefcnt;			/* cluster reference counts */
extern int max_linkhdr;			/* largest link-level header */
extern int max_protohdr;		/* largest protocol header */
extern int max_hdr;				/* largest link+protocol header */
extern int max_datalen;			/* MHLEN - max_hdr */
extern int m_want;

struct mbuf 	*m_copy(struct mbuf *, int, int);
struct mbuf 	*m_free(struct mbuf *);
struct mbuf 	*m_get(int, int);
u_int			m_length(struct mbuf *);
struct mbuf 	*m_gethdr(int, int);
struct mbuf 	*m_getclr(int, int);
void            m_clget(struct mbuf *, int);
struct mbuf 	*m_prepend(struct mbuf *, int, int);
struct mbuf 	*m_pullup(struct mbuf *, int);
struct	mbuf 	*m_pulldown(struct mbuf *, int, int, int *);
struct mbuf 	*m_copyup(struct mbuf *, int, int);
struct mbuf 	*m_split(struct mbuf *, int, int);
struct mbuf 	*m_devget(char *, int, int, struct ifnet *, void (*)());
struct mbuf 	*m_retry(int, int);
struct mbuf 	*m_retryhdr(int, int);
void			m_cat(struct mbuf *, struct mbuf *);
void			m_adj(struct mbuf *, int);
int	        	m_apply(struct mbuf *, int, int, int (*)(void *, caddr_t, unsigned int), void *);
int			    m_clalloc(int, int);
void			m_copydata(struct mbuf *, int, int, void *);
int				m_makewritable(struct mbuf **, int, int);
void			m_freem(struct mbuf *);
void			m_reclaim(void);
void 			mbinit2(void *, int, int);
struct mbuf 	*m_getptr(struct mbuf *, int, int *);
void			m_remove_pkthdr(struct mbuf *);
void			m_copy_pkthdr(struct mbuf *, struct mbuf *);
void			m_move_pkthdr(struct mbuf *, struct mbuf *);

#define m_copyback(m, off, len, cp)	\
	(m_copydata(m, off, len, cp))

/* Packet tag routines */
struct m_tag 	*m_tag_get(int, int, int);
void			m_tag_free(struct m_tag *);
void			m_tag_prepend(struct mbuf *, struct m_tag *);
void			m_tag_unlink(struct mbuf *, struct m_tag *);
void			m_tag_delete(struct mbuf *, struct m_tag *);
void			m_tag_delete_chain(struct mbuf *);
void			m_tag_delete_nonpersistent(struct mbuf *);
struct m_tag 	*m_tag_find(struct mbuf *, int, struct m_tag *);
struct m_tag 	*m_tag_copy(struct m_tag *);
int				m_tag_copy_chain(struct mbuf *, struct mbuf *);
void			m_tag_init(struct mbuf *);
struct m_tag 	*m_tag_first(struct mbuf *);
struct m_tag 	*m_tag_next(struct mbuf *, struct m_tag *);

/* Packet tag types */
#define PACKET_TAG_NONE						0  /* Nothing */
#define PACKET_TAG_VLAN						1  /* VLAN ID */
#define PACKET_TAG_ENCAP					2  /* encapsulation data */
#define PACKET_TAG_ESP						3  /* ESP information */
#define PACKET_TAG_PF_GENERATED				11 /* PF generated, pass always */
#define PACKET_TAG_PF_ROUTED				12 /* PF routed, no route loops */
#define PACKET_TAG_PF_FRAGCACHE				13 /* PF fragment cached */
#define PACKET_TAG_PF_QID					14 /* PF queue id */
#define PACKET_TAG_PF_TAG					15 /* PF tags */

#define PACKET_TAG_IPSEC_IN_CRYPTO_DONE		16
#define PACKET_TAG_IPSEC_IN_DONE			17
#define PACKET_TAG_IPSEC_OUT_DONE			18
#define	PACKET_TAG_IPSEC_OUT_CRYPTO_NEEDED	19  /* NIC IPsec crypto req'ed */
#define	PACKET_TAG_IPSEC_IN_COULD_DO_CRYPTO	20  /* NIC notifies IPsec */
#define	PACKET_TAG_IPSEC_PENDING_TDB		21  /* Reminder to do IPsec */

#define	PACKET_TAG_IPSEC_SOCKET				22 /* IPSEC socket ref */
#define	PACKET_TAG_IPSEC_HISTORY			23 /* IPSEC history */

#define	PACKET_TAG_PF_TRANSLATE_LOCALHOST	24 /* translated to localhost */
#define	PACKET_TAG_IPSEC_NAT_T_PORTS		25 /* two u_int16_t */

#ifdef MBTYPES
int mbtypes[] = {				/* XXX */
	M_FREE,		/* MT_FREE		0	   should be on free list */
	M_MBUF,		/* MT_DATA		1	   dynamic (data) allocation */
	M_MBUF,		/* MT_HEADER	2	   packet header */
	M_SOCKET,	/* MT_SOCKET	3	   socket structure */
	M_PCB,		/* MT_PCB		4	   protocol control block */
	M_RTABLE,	/* MT_RTABLE	5	   routing tables */
	M_HTABLE,	/* MT_HTABLE	6	   IMP host tables */
	0,			/* MT_ATABLE	7	   address resolution tables */
	M_MBUF,		/* MT_SONAME	8	   socket name */
	0,					/* 		9 */
	M_SOOPTS,	/* MT_SOOPTS	10	   socket options */
	M_FTABLE,	/* MT_FTABLE	11	   fragment reassembly header */
	M_MBUF,		/* MT_RIGHTS	12	   access rights */
	M_IFADDR,	/* MT_IFADDR	13	   interface address */
	M_MBUF,		/* MT_CONTROL	14	   extra-data protocol message */
	M_MBUF,		/* MT_OOBDATA	15	   expedited data  */
#ifdef DATAKIT
	25, 26, 27, 28, 29, 30, 31, 32	/* datakit ugliness */
#endif
};
#endif
#endif
#endif /* _SYS_MBUF_H_ */
