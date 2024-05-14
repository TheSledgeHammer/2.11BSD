/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include <sys/cdefs.h>

#ifndef lint
static char sccsid[] = "@(#)libmai.c	5.1 (Berkeley) 8/9/85";
#endif not lint

#include <stdlib.h>

int yyparse(void);

int
main(int argc, char *argv[])
{
	return yyparse();
}
