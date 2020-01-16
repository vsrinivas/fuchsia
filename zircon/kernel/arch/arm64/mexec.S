// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/cache_loop.h>
#include <arch/asm_macros.h>
#include <arch/defines.h>
#include <asm.h>
#include <mexec.h>

/* Arguments Passed via x0 through x8 inclusive */
bootarg0                .req x25
bootarg1                .req x26
bootarg2                .req x27
boot_el                 .req x28

// This is the address to branch to once the copy is completed.
new_kernel_addr         .req x24

// This is a null terminated list of memory regions to copy.
copy_list               .req x23

// This many more entires in the current page to copy before we should
// interpret the entry as a pointer to the next page in the linked list.
page_remaining_entries  .req x22

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

// Stash the boot el, as we will need it later
    mov     boot_el,  x3

// This list contains the memmove operations that we need to perform.
    mov     copy_list, x4

// This is the address of the kernel that we eventueally want to jump to.
    mov     new_kernel_addr, x5

    // If we were originally booted in EL2, transition back into EL2
    cmp  boot_el, #2
    b.lt cplt_transition_to_boot_el  // We booted in EL1, no need to transition up
    adr  x0, cplt_transition_to_boot_el
    hvc  #1     // Branch into an EL2 trampoline that bounces to cplt_transition_to_boot_el

 cplt_transition_to_boot_el:

    // Clean/Invalidate the cache early on.
    // We want to make sure that there are no dirty cache entries hanging around
    // in the cache before we start the memcpy.
    // If these cache entries were to get written back later, they would corrupt
    // the state of the system so we clean/invalidate them up front.
    bl      mexec_arch_clean_invalidate_cache_all

// Outer loop: Execute all the memmove ops in a given page.
.Lcopypage:
    ldr page_remaining_entries, =(MAX_OPS_PER_PAGE)

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
    // in the copy list are null.
    orr     tmp, x0, x1
    orr     tmp, tmp, x2
    cbz     tmp, .Lfinish_copy

    // A page may contain up to 169* memmove ops, the 170th op is actually a
    // pointer to the next page full of memmove ops.
    cbnz    page_remaining_entries, .Lcontinue_page     // Have we processed 169 entries yet, branch if we haven't.
    mov     copy_list, x0   // If we've followed all the entries on the current page, follow the next pointer of the linked list.
    b       .Lcopypage      // And execute the list of copies on the page that we just advanced to.
.Lcontinue_page:

    // The copy operation is not null, go ahead and memmove
    bl      memmove_mexec

    // Advance the pointer to the next copy operation.
    add     copy_list, copy_list, MEMMOV_OPS_STRUCT_LEN

    // Decrement the number of entries remaining in this page, if this reaches 0
    // we must follow the linked list to the next page.
    sub     page_remaining_entries, page_remaining_entries, 1

    b       .Lcopy

.Lfinish_copy:

    bl mexec_arch_clean_invalidate_cache_all

    // Restore the bootarguments for the next kernel.
    mov     x0, bootarg0
    mov     x1, bootarg1
    mov     x2, bootarg2

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
LOCAL_FUNCTION(mexec_arch_clean_invalidate_cache_all)
    cache_way_set_op cisw, clean_invalidate

    // Invalidate the instruction cache as well
    ic      iallu
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
