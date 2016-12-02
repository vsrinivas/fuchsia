/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 *	@(#)nodes.c.pat	8.2 (Berkeley) 5/4/95
 */

#include <magenta/syscalls.h>
#include <stdlib.h>

/*
 * Routine for dealing with parsed shell commands.
 */

#include "shell.h"
#include "nodes.h"
#include "memalloc.h"
#include "machdep.h"
#include "mystring.h"
#include "system.h"
#include "error.h"


int     funcblocksize;		/* size of structures in function */
int     funcstringsize;		/* size of strings in node */
pointer funcblock;		/* block to allocate function from */
char   *funcstring;		/* block to allocate strings from */

%SIZES


STATIC void calcsize(union node *);
STATIC void sizenodelist(struct nodelist *);
STATIC union node *copynode(union node *);
STATIC struct nodelist *copynodelist(struct nodelist *);
STATIC char *nodesavestr(char *);

STATIC void writenode(union node *n, size_t node_size, size_t block_size);
STATIC void encodenode(union node *);
STATIC void encodenodelist(struct nodelist *);
STATIC void encodestring(const char *);

STATIC void decodenode(union node **);
STATIC void decodenodelist(struct nodelist **);
STATIC char *decodestring();

/*
 * Make a copy of a parse tree.
 */

struct funcnode *
copyfunc(union node *n)
{
	struct funcnode *f;
	size_t blocksize;

	funcblocksize = offsetof(struct funcnode, n);
	funcstringsize = 0;
	calcsize(n);
	blocksize = funcblocksize;
	f = ckmalloc(blocksize + funcstringsize);
	funcblock = (char *) f + offsetof(struct funcnode, n);
	funcstring = (char *) f + blocksize;
	copynode(n);
	f->count = 0;
	return f;
}



STATIC void
calcsize(n)
	union node *n;
{
	%CALCSIZE
}



STATIC void
sizenodelist(lp)
	struct nodelist *lp;
{
	while (lp) {
		funcblocksize += SHELL_ALIGN(sizeof(struct nodelist));
		calcsize(lp->n);
		lp = lp->next;
	}
}



STATIC union node *
copynode(n)
	union node *n;
{
	union node *new;

	%COPY
	return new;
}


STATIC struct nodelist *
copynodelist(lp)
	struct nodelist *lp;
{
	struct nodelist *start;
	struct nodelist **lpp;

	lpp = &start;
	while (lp) {
		*lpp = funcblock;
		funcblock = (char *) funcblock +
		    SHELL_ALIGN(sizeof(struct nodelist));
		(*lpp)->n = copynode(lp->n);
		lp = lp->next;
		lpp = &(*lpp)->next;
	}
	*lpp = NULL;
	return start;
}



STATIC char *
nodesavestr(s)
	char   *s;
{
	char   *rtn = funcstring;

	funcstring = stpcpy(funcstring, s) + 1;
	return rtn;
}

STATIC void writenode(union node *n, size_t node_size, size_t block_size)
{
	if (block_size > funcblocksize) {
		sh_error("Unable to encode AST");
		exraise(-1);
	}
	memcpy(funcblock, n, node_size);
	funcblock = (char *) funcblock + block_size;
	funcblocksize -= block_size;
}

STATIC void
encodenode(union node *n)
{
	%ENCODE
}

STATIC void
encodenodelist(struct nodelist *lp)
{
	while (lp) {
		memcpy(funcblock, lp, sizeof(struct nodelist));
		funcblock = (char *) funcblock + SHELL_ALIGN(sizeof(struct nodelist));
		encodenode(lp->n);
		lp = lp->next;
	}
}

STATIC void
encodestring(const char *s)
{
	funcstring = stpcpy(funcstring, s) + 1;
}


STATIC void
decodenode(union node **npp)
{
	%DECODE
}

STATIC void
decodenodelist(struct nodelist **lpp)
{
	while (*lpp) {
		*lpp = funcblock;
		funcblock = (char *) funcblock + SHELL_ALIGN(sizeof(struct nodelist));
		struct nodelist *lp = *lpp;
		decodenode(&lp->n);
		lpp = &lp->next;
	}
}

STATIC char *
decodestring()
{
	char *result = funcstring;
	funcstring += strlen(result) + 1;
	return result;
}

/*
 * Free a parse tree.
 */

void
freefunc(struct funcnode *f)
{
	if (f && --f->count < 0)
		ckfree(f);
}

static const size_t kHeaderSize = SHELL_ALIGN(sizeof(funcblocksize));

// The encoded format contains three segments:
//
// * A 32 bit integer that contains the length in bytes of the next segment.
// * A sequence of nodes in a pre-order traversal of the node tree.
//   - The encoded size of each node is determined by its type.
//   - Pointer fields in each node contain zero if that pointer should decode
//     a NULL. Otherwise, if the pointer should decode as non-NULL, the field
//     contains an arbitrary non-zero value. (These values are the address of
//     the node or the string in the encoding process, which isn't meaningful to
//     the decoding progress).
// * A sequence of null-terminated strings, in the order the strings are
//   encountered in a pre-order traversal of the node tree.

mx_status_t
codec_encode(union node *node, mx_handle_t *vmo)
{
	funcblocksize = 0;
	funcstringsize = 0;
	calcsize(node);
	const size_t size = kHeaderSize + funcblocksize + funcstringsize;
	char buffer[size];
	memcpy(buffer, &funcblocksize, sizeof(funcblocksize));
	funcblock = buffer + kHeaderSize;
	funcstring = buffer + kHeaderSize + funcblocksize;
	encodenode(node);
	mx_status_t status = mx_vmo_create(size, 0, vmo);
	if (status != NO_ERROR)
		return status;
	mx_size_t actual;
	return mx_vmo_write(*vmo, buffer, 0, size, &actual);
}

union node *codec_decode(char *buffer, size_t length)
{
	// TODO(abarth): Validate the length.
	memcpy(&funcblocksize, buffer, sizeof(funcblocksize));
	funcblock = buffer + kHeaderSize;
	funcstring = buffer + kHeaderSize + funcblocksize;
	union node dummy;
	// We need to use a real union node address to avoid undefined behavior.
	union node *node = &dummy;
	decodenode(&node);
	return node;
}

