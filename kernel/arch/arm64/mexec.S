// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/asm_macros.h>
#include <arch/defines.h>
#include <asm.h>
#include <mexec.h>

/* Arguments Passed via x0 through x8 inclusive */
bootarg0                .req x25
bootarg1                .req x26
bootarg2                .req x27
bootarg3                .req x28

// This is a null terminated list of memory regions to copy.
copy_list               .req x23

// This is the address to branch to once the copy is completed.
new_kernel_addr         .req x24

tmp                     .req x9

.section .text
FUNCTION(mexec_asm)

// Turn off the caches and MMU
    mrs     tmp, sctlr_el1      // Read the SCTLR into a temp
    bic     tmp, tmp, #(1<<12)  // Disable icache
    bic     tmp, tmp, #(1<<2)   // Disable dcache/ucache
    bic     tmp, tmp, #(1<<0)   // Disable the MMU
    msr     sctlr_el1, tmp      // Write the temp back to the control register

// Stash the boot arguments to pass to the next kernel since we expect to trash
// x0 - x5
    mov     bootarg0, x0
    mov     bootarg1, x1
    mov     bootarg2, x2
    mov     bootarg3, x3

// This list contains the memmove operations that we need to perform.
    mov     copy_list, x4

// This is the address of the kernel that we eventueally want to jump to.
    mov     new_kernel_addr, x5

    // Clean/Invalidate the cache early on.
    // We want to make sure that there are no dirty cache entries hanging around
    // in the cache before we start the memcpy.
    // If these cache entries were to get written back later, they would corrupt
    // the state of the system so we clean/invalidate them up front.
    bl      mexec_arch_clean_invalidate_cache_all

/* Mempy the new kernel over the old kernel. Keep in mind that since the MMU
 * is disabled, unaligned accesses are no longer legal. All accesses must be
 * word aligned.
 */
.Lcopy:
    // Load a copy operation into memory
    ldr     x0, [copy_list, MEMMOV_OPS_DST_OFFSET]
    ldr     x1, [copy_list, MEMMOV_OPS_SRC_OFFSET]
    ldr     x2, [copy_list, MEMMOV_OPS_LEN_OFFSET]

    // Determine if this is the end of the list by checking if all three elems
    // in the copy list are null
    orr     tmp, x0, x1
    orr     tmp, tmp, x2
    cbz     tmp, .Lfinish_copy

    // The copy operation is not null, go ahead and memmove
    bl      memmove_mexec

    // Advance the pointer to the next copy operation.
    add     copy_list, copy_list, 24

    b       .Lcopy

.Lfinish_copy:

    bl mexec_arch_clean_invalidate_cache_all

    // Restore the bootarguments for the next kernel.
    mov     x0, bootarg0
    mov     x1, bootarg1
    mov     x2, bootarg2
    mov     x3, bootarg3

    // Get everything out of the pipeline before branching to the new kernel.
    isb
    dsb sy

    // Branch to the next kernel.
    br      new_kernel_addr
END_FUNCTION(mexec_asm)

LOCAL_FUNCTION(memmove_mexec)
        // x6 contains the stride (1 word if we're copying forward
        // -1 word if we're copying backwards)
        mov     x6, 1

        // x3 is the start index of the copy, this is the front of the array if
        // we're copying forward or the back of the array if we're copying
        // backwards.
        mov     x3, 0

        // Convert the length of the array from bytes to machine words
        lsr     x2, x2, 3

        // If the source address and the destination address are the same then
        // we can return because there's nothing to be done.
        cmp     x0, x1
        beq     .done

        // Decide if we need to copy backwards.
        blt     .no_alias
        mov     x6, -1          // Set the stride to backwards
        mov     x3, x2          // Move the copy index to the back of the array
        sub     x3, x3, 1       // i = (len_wrds - 1); to start at the last word

.no_alias:
        mov     x4, 0           // Loop iteration index
.copy_loop:
        // Copy one word of data
        // dst[i << 3] = src[i << 3]
        ldr     tmp, [x1, x3, lsl 3]
        str     tmp, [x0, x3, lsl 3]

        lsl     x7, x3, 3
        add     x7, x7, x0

        // Increment the array index by the stride (backwards or forwards).
        // i += stride
        add     x3, x3, x6

        // Increment the number of words copied (we use this to decide when to
        // stop)
        // words_copied += 1
        add     x4, x4, 1

        // If we've copied the whole buffer, then finish.
        // if (words_copied == words_to_copy) break;
        cmp     x2, x4
        bne     .copy_loop
.done:
        ret
END_FUNCTION(memmove_mexec)

// Perform a bulk clean/invalidate across the whole cache
// Normally on ARM we can use the CIVAC, CVAC, CVAU and IVAC instructions to
// manipulate the cache but these ops only work against virtual memory addresses
// and since we have disabled the MMU, these instructions are no longer
// meaningful.
// As a result, we have to use the Level/Set/Way cache ops. Since the definition
// of the cache set is left up to the implementation, the only portable (safe)
// way to perform these cache ops is to operate against the whole cache.
// The following op cleans and invalidates every entry in each level of the
// cache.
// The original implementation can be found in the ARMv8-A TRM or at the
// following URL: http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.den0024a/BABJDBHI.html
LOCAL_FUNCTION(mexec_arch_clean_invalidate_cache_all)
         mrs x0, clidr_el1
         and w3, w0, #0x07000000  // get 2 x level of coherence
         lsr w3, w3, #23
         cbz w3, finished2
         mov w10, #0              // w10 = 2 x cache level
         mov w8, #1               // w8 = constant 0b1
  loop12: add w2, w10, w10, lsr #1 // calculate 3 x cache level
         lsr w1, w0, w2           // extract 3-bit cache type for this level
         and w1, w1, #0x7
         cmp w1, #2
         b.lt skip2                // no data or unified cache at this level
         msr csselr_el1, x10      // select this cache level
         isb                      // synchronize change of csselr
         mrs x1, ccsidr_el1       // read ccsidr
         and w2, w1, #7           // w2 = log2(linelen)-4
         add w2, w2, #4           // w2 = log2(linelen)
         ubfx w4, w1, #3, #10     // w4 = max way number, right aligned
         clz w5, w4               /* w5 = 32-log2(ways), bit position of way in dc                                    operand */
         lsl w9, w4, w5           /* w9 = max way number, aligned to position in dc
                                     operand */
         lsl w16, w8, w5          // w16 = amount to decrement way number per iteration
  loop22: ubfx w7, w1, #13, #15    // w7 = max set number, right aligned
         lsl w7, w7, w2           /* w7 = max set number, aligned to position in dc
                                     operand */
         lsl w17, w8, w2          // w17 = amount to decrement set number per iteration
  loop33: orr w11, w10, w9         // w11 = combine way number and cache number...
         orr w11, w11, w7         // ... and set number for dc operand
         dc cisw, x11              // do data cache clean by set and way
         subs w7, w7, w17         // decrement set number
         b.ge loop33
         subs x9, x9, x16         // decrement way number
         b.ge loop22
  skip2:  add w10, w10, #2         // increment 2 x cache level
         cmp w3, w10
         dsb sy                      /* ensure completion of previous cache maintenance
                                    //  operation */
         b.gt loop12
  finished2:
         ic iallu
         isb
         dsb sy

         ret
END_FUNCTION(mexec_arch_clean_invalidate_cache_all)


/* This .ltorg emits any immediate constants here. We need to put this before
 * the mexec_asm_end symbol because we intend to relocate the assembly contained
 * within the mexec_asm[_end] block. Any constants needed by this block should
 * also be relocated so we need to ensure that they occur before mexec_asm_end.
 */
.ltorg

DATA(mexec_asm_end)
