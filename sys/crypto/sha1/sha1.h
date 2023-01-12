/*	$NetBSD: sha1.h,v 1.3 2003/07/08 06:18:00 itojun Exp $	*/

/*
 * SHA-1 in C
 * By Steve Reid <steve@edmweb.com>
 * 100% Public Domain
 */

#ifndef _SYS_SHA1_H_
#define	_SYS_SHA1_H_

#include <sys/cdefs.h>
#include <sys/types.h>

typedef struct {
	u_int32_t state[5];
	u_int32_t count[2];  
	u_char buffer[64];
} SHA1_CTX;
  
void	SHA1Transform(u_int32_t[5], const u_char[64]);
void	SHA1Init(SHA1_CTX *);
void	SHA1Update(SHA1_CTX *, const u_char *, u_int);
void	SHA1Final(u_char[20], SHA1_CTX *);
#ifndef _KERNEL
char	*SHA1End(SHA1_CTX *, char *);
char	*SHA1File(char *, char *);
char	*SHA1Data(const u_char *, size_t, char *);
#endif /* _KERNEL */

#endif /* _SYS_SHA1_H_ */