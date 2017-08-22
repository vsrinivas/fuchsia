/*
 * Non-physical true random number generator based on timing jitter.
 *
 * Copyright Stephan Mueller <smueller@chronox.de>, 2014
 *
 * License
 * =======
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Modifications by the Fuchsia Authors, 2017
 * =======
 *
 * - Remove references to jitterentropy-base-{kernel,user}.h.
 * - Add __BEGIN/END_CDECLS.
 * - Add #include lines for required system libraries.
 * - Remove CONFIG_CRYPTO_CPU_JITTERENTROPY_STAT flag.
 * - Add jent_entropy_collector_init declaration.
 * - Moved comment for jent_lfsr_var_stat from jitterentropy-base.c to here.
 */

#ifndef _JITTERENTROPY_H
#define _JITTERENTROPY_H

#include <magenta/compiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

__BEGIN_CDECLS;

/* The entropy pool */
struct rand_data
{
	/* all data values that are vital to maintain the security
	 * of the RNG are marked as SENSITIVE. A user must not
	 * access that information while the RNG executes its loops to
	 * calculate the next random value. */
	uint64_t data;		/* SENSITIVE Actual random number */
	uint64_t old_data;	/* SENSITIVE Previous random number */
	uint64_t prev_time;	/* SENSITIVE Previous time stamp */
#define DATA_SIZE_BITS ((sizeof(uint64_t)) * 8)
	uint64_t last_delta;	/* SENSITIVE stuck test */
	int64_t last_delta2;	/* SENSITIVE stuck test */
	unsigned int osr;	/* Oversample rate */
	int fips_enabled;	/* FIPS enabled? */
	unsigned int stir:1;		/* Post-processing stirring */
	unsigned int disable_unbias:1;	/* Deactivate Von-Neuman unbias */
#define JENT_MEMORY_BLOCKS 64
#define JENT_MEMORY_BLOCKSIZE 32
#define JENT_MEMORY_ACCESSLOOPS 128
#define JENT_MEMORY_SIZE (JENT_MEMORY_BLOCKS*JENT_MEMORY_BLOCKSIZE)
	unsigned char *mem;	/* Memory access location with size of
				 * memblocks * memblocksize */
	unsigned int memlocation; /* Pointer to byte in *mem */
	unsigned int memblocks;	/* Number of memory blocks in *mem */
	unsigned int memblocksize; /* Size of one memory block in bytes */
	unsigned int memaccessloops; /* Number of memory accesses per random
				      * bit generation */
};

/* Flags that can be used to initialize the RNG */
#define JENT_DISABLE_STIR (1<<0) /* Disable stirring the entropy pool */
#define JENT_DISABLE_UNBIAS (1<<1) /* Disable the Von-Neuman Unbiaser */
#define JENT_DISABLE_MEMORY_ACCESS (1<<2) /* Disable memory access for more
					     entropy, saves MEMORY_SIZE RAM for
					     entropy collector */

/* -- BEGIN Main interface functions -- */

#ifndef JENT_STUCK_INIT_THRES
/*
 * Per default, not more than 90% of all measurements during initialization
 * are allowed to be stuck.
 *
 * It is allowed to change this value as required for the intended environment.
 */
#define JENT_STUCK_INIT_THRES(x) (x/10 * 9)
#endif

#ifdef JENT_PRIVATE_COMPILE
# define JENT_PRIVATE_STATIC static
#else /* JENT_PRIVATE_COMPILE */
# define JENT_PRIVATE_STATIC
#endif

/* Number of low bits of the time value that we want to consider */
/* get raw entropy */
JENT_PRIVATE_STATIC
ssize_t jent_read_entropy(struct rand_data *ec, char *data, size_t len);
/* initialize an instance of the entropy collector */
JENT_PRIVATE_STATIC
struct rand_data *jent_entropy_collector_alloc(unsigned int osr,
	       				       unsigned int flags);
/* clearing of entropy collector */
JENT_PRIVATE_STATIC
void jent_entropy_collector_free(struct rand_data *entropy_collector);

/* initialization of entropy collector */
JENT_PRIVATE_STATIC
int jent_entropy_init(void);

/* return version number of core library */
JENT_PRIVATE_STATIC
unsigned int jent_version(void);

/* -- END of Main interface functions -- */

/* -- BEGIN error codes for init function -- */
#define ENOTIME  	1 /* Timer service not available */
#define ECOARSETIME	2 /* Timer too coarse for RNG */
#define ENOMONOTONIC	3 /* Timer is not monotonic increasing */
#define EMINVARIATION	4 /* Timer variations too small for RNG */
#define EVARVAR		5 /* Timer does not produce variations of variations
			     (2nd derivation of time is zero) */
#define EMINVARVAR	6 /* Timer variations of variations is too small */
#define EPROGERR	7 /* Programming error */
#define ESTUCK		8 /* Too many stuck results during init. */

/* -- BEGIN statistical test functions only complied with CONFIG_CRYPTO_CPU_JITTERENTROPY_STAT -- */

/*
 * Statistical test: return the time duration for the folding operation. If
 * lfsr_loops_override/mem_loops_override is non-zero, perform the given number
 * of LFSR/memaccess ops. Otherwise, allow the loop count shuffling to define
 * the number of LFSR/memaccess ops.
 */
JENT_PRIVATE_STATIC
uint64_t jent_lfsr_var_stat(struct rand_data *ec,
                            unsigned int lfsr_loops_override,
                            unsigned int mem_loops_override);

/* -- END of statistical test function -- */

/* -- BEGIN Magenta interface -- */

/* Initialize an entropy collector using already allocated memory. This function
 * is to jent_entropy_collector_alloc as placement new is to regular new in C++.
 *
 * |ec| is the entropy collector to initialize. |mem| points to a block of
 * |mem_size| bytes used for memory access loops (to generate CPU instruction
 * time variation).
 *
 * The memory will be logically divided into |mem_block_count| blocks of size
 * |mem_block_size|; it is an error if the product of these two values is larger
 * than |mem_size|. Ideally, the mem_block_* parameters should be configured for
 * each target, or at least each architecture. The entropy collector will
 * perform at least |mem_loops| memory access loops to generate variations.
 *
 * The |stir| flag controls whether to stir a deterministic constant into the
 * entropy pool, which does not destroy entropy but may whiten it.
 */
void jent_entropy_collector_init(
        struct rand_data* ec, uint8_t* mem, size_t mem_size,
        unsigned int mem_block_size, unsigned int mem_block_count,
        unsigned int mem_loops, bool stir);

/* -- END of Magenta interface -- */

__END_CDECLS;

#endif /* _JITTERENTROPY_H */
