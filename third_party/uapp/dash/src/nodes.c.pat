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
#include "var.h"


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

// Fuchsia-specific:
// This is the definition of the header of the VMO used for transferring initialization
// information to a subshell.  This information would be automatically inherited if we
// were able to invoke the subshell using a fork().
// For now, we pass symbol table information (non-exported symbols, those are passed in
// the environment) and a list of operations to be performed by the subshell.
struct state_header
{
	size_t num_symbols;
	size_t symtab_offset;
	size_t cmd_offset;
	size_t string_offset;
};
static const size_t kHeaderSize = SHELL_ALIGN(sizeof(struct state_header));

static char *ignored_syms[] = { "_", "PPID", "PWD" };

static bool
ignore_sym(char *name)
{
	for (size_t sym_ndx = 0;
	     sym_ndx < sizeof(ignored_syms) / sizeof(char *);
	     sym_ndx++) {
		if (!strcmp(ignored_syms[sym_ndx], name)) {
			return true;
		}
	}
	return false;
}

// Determine the space needed to represent the NULL-terminated symbol table
// 'vars'. Also sets 'num_vars' to the number of symbol table entries.
static size_t
calc_symtab_size(char **vars, size_t *num_vars)
{
	size_t total_len = 0;
	*num_vars = 0;
	while (*vars) {
		if (! ignore_sym(*vars)) {
			// + 2 for NULL symbol flags
			total_len += strlen(*vars) + 2;
			(*num_vars)++;
		}
		vars++;
	}
	return total_len;
}

// Write symbols into 'buffer'. If 'is_readonly' is set, all variables are
// marked as such.
static size_t
output_symtab(char *buffer, char **vars, bool is_readonly)
{
	char *orig_buffer = buffer;
	while (*vars) {
		if (! ignore_sym(*vars)) {
			*buffer++ = is_readonly ? 1 : 0;
			size_t len = strlen(*vars);
			buffer = mempcpy(buffer, *vars, len + 1);
		}
		vars++;
	}
	return buffer - orig_buffer;
}

// Read in symbols from the encoded table 'buffer'. We currently only support
// two variants of variables: readonly (flags == 1) and writable (flags == 0).
static void
restore_symtab(char *buffer, size_t num_syms)
{
	while(num_syms--) {
		bool is_readonly = (*buffer++ == 1);
		setvareq(buffer, is_readonly ? VREADONLY : 0);
		buffer += (strlen(buffer) + 1);
	}
}

// The encoded format contains four segments:
//
// * A header that specifies the number of symbols, and offsets of each of
//   the three segments (see "struct state_header").
// * A symbol table. Each entry in the symbol table is a single-byte of flags
//   (1 = read-only, 0 = writable) followed by a NULL-terminted NAME=VALUE
//   string.
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
codec_encode(struct nodelist *nlist, mx_handle_t *vmo)
{
	funcblocksize = 0;
	funcstringsize = 0;
	char **writable_vars = listvars(0, VEXPORT | VREADONLY, 0);
	char **readonly_vars = listvars(VREADONLY, VEXPORT, 0);

	// Calculate the size of the components
	size_t num_writable_vars;
	size_t num_readonly_vars;
	size_t total_symtab_size = calc_symtab_size(writable_vars, &num_writable_vars) +
				   calc_symtab_size(readonly_vars, &num_readonly_vars);
	total_symtab_size = SHELL_ALIGN(total_symtab_size);
	sizenodelist(nlist);
	struct state_header header;

	// Fill in the header
	header.num_symbols = num_writable_vars + num_readonly_vars;
	header.symtab_offset = kHeaderSize;
	header.cmd_offset = header.symtab_offset + total_symtab_size;
	header.string_offset = header.cmd_offset + funcblocksize;

	const size_t total_size = header.string_offset + funcstringsize;
	char buffer[total_size];

	// Output the symbol tables
	memcpy(buffer, &header, sizeof(header));
	size_t symtab_offset = header.symtab_offset;
	symtab_offset += output_symtab(&buffer[symtab_offset], writable_vars, 0);
	output_symtab(&buffer[symtab_offset], readonly_vars, 1);

	// Output the command nodes
	funcblock = buffer + header.cmd_offset;
	funcstring = buffer + header.string_offset;
	encodenodelist(nlist);

	// And VMO-ify the whole thing
	mx_status_t status = mx_vmo_create(total_size, 0, vmo);
	if (status != MX_OK)
		return status;
	size_t actual;
	return mx_vmo_write(*vmo, buffer, 0, total_size, &actual);
}

struct nodelist *codec_decode(char *buffer, size_t length)
{
	// TODO(abarth): Validate the length.
	struct state_header header;
	memcpy(&header, buffer, sizeof(header));

	restore_symtab(buffer + header.symtab_offset, header.num_symbols);
	funcblock = buffer + header.cmd_offset;
	funcstring = buffer + header.string_offset;
	struct nodelist dummy;
	// The decodenodelist API is very... unique. It needs the
	// argument to point to something non-NULL, even though the
	// argument is otherwise ignored and used as an output parameter.
	struct nodelist *nlist = &dummy;
	decodenodelist(&nlist);
	return nlist;
}

