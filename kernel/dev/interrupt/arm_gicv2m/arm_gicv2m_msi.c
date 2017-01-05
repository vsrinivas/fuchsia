// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


/* If we are building with PCIe support, go ahead and implement management of
 * MSI IRQ blocks here, instead of requiring each platform to do so on their
 * own. */
#if WITH_DEV_PCIE

#include <dev/interrupt/arm_gicv2m.h>
#include <dev/interrupt/arm_gicv2m_msi.h>
#include <dev/pcie_platform.h>
#include <dev/pci_common.h>
#include <lib/pow2_range_allocator.h>
#include <pow2.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>

p2ra_state_t g_32bit_targets;
p2ra_state_t g_64bit_targets;

status_t arm_gicv2m_msi_init(void) {
    status_t ret;

    ret = p2ra_init(&g_32bit_targets, PCIE_MAX_MSI_IRQS);
    if (ret != NO_ERROR) {
        TRACEF("Failed to initialize 32 bit allocation pool!\n");
        return ret;
    }

    ret = p2ra_init(&g_64bit_targets, PCIE_MAX_MSI_IRQS);
    if (ret != NO_ERROR) {
        TRACEF("Failed to initialize 64 bit allocation pool!\n");
        p2ra_free(&g_32bit_targets);
        return ret;
    }

    /* TODO(johngro)
     *
     * Right now, the pow2 range allocator will not accept overlapping ranges.
     * It may be possible for fancy GIC implementations to have multiple MSI
     * frames aligned on 4k boundaries (for virtualisation) with either
     * completely or partially overlapping IRQ ranges.  If/when we need to deal
     * with hardware like this, we will need to come back here and make this
     * system more sophisticated.
     */
    arm_gicv2m_frame_info_t info;
    for (uint i = 0; arm_gicv2m_get_frame_info(i, &info) == NO_ERROR; ++i) {
        p2ra_state_t* pool = ((uint64_t)info.doorbell & 0xFFFFFFFF00000000)
                           ? &g_64bit_targets
                           : &g_32bit_targets;

        uint len = info.end_spi_id - info.start_spi_id + 1;
        ret = p2ra_add_range(pool, info.start_spi_id, len);
        if (ret != NO_ERROR) {
            TRACEF("Failed to add MSI IRQ range [%u, %u] to allocator (ret %d).\n",
                    info.start_spi_id, len, ret);
            goto finished;
        }
    }

finished:
    if (ret != NO_ERROR) {
        p2ra_free(&g_32bit_targets);
        p2ra_free(&g_64bit_targets);
    }

    return ret;
}

status_t arm_gicv2m_alloc_msi_block(uint requested_irqs,
                                    bool can_target_64bit,
                                    bool is_msix,
                                    pcie_msi_block_t* out_block) {
    if (!out_block)
        return ERR_INVALID_ARGS;

    if (out_block->allocated)
        return ERR_BAD_STATE;

    if (!requested_irqs || (requested_irqs > PCIE_MAX_MSI_IRQS))
        return ERR_INVALID_ARGS;

    status_t ret = ERR_INTERNAL;
    bool is_32bit = false;
    uint alloc_size = 1u << log2_uint_ceil(requested_irqs);
    uint alloc_start;

    /* If this MSI request can tolerate a 64 bit target address, start by
     * attempting to allocate from the 64 bit pool */
    if (can_target_64bit)
        ret = p2ra_allocate_range(&g_64bit_targets, alloc_size, &alloc_start);

    /* No allocation yet?  Fall back on the 32 bit pool */
    if (ret != NO_ERROR) {
        ret = p2ra_allocate_range(&g_32bit_targets, alloc_size, &alloc_start);
        is_32bit = true;
    }

    /* If we have not managed to allocate yet, then we fail */
    if (ret != NO_ERROR)
        return ret;

    /* Find the target physical address for this allocation.
     *
     * TODO(johngro) : we could make this O(k) instead of O(n) by associating a
     * context pointer with ranges registered with the pow2 allocator.  Right
     * now, however, N tends to be 1, so it is difficult to be too concerned
     * about this.
     */
    arm_gicv2m_frame_info_t info;
    for (uint i = 0; (ret = arm_gicv2m_get_frame_info(i, &info)) == NO_ERROR; ++i) {
        uint alloc_end = alloc_start + alloc_size - 1;

        if (((alloc_start >= info.start_spi_id) && (alloc_start <= info.end_spi_id)) &&
            ((alloc_end   >= info.start_spi_id) && (alloc_end   <= info.end_spi_id)))
            break;
    }

    /* This should never ever fail */
    DEBUG_ASSERT(ret == NO_ERROR);
    if (ret != NO_ERROR) {
        p2ra_free_range(is_32bit ? &g_32bit_targets : &g_64bit_targets, alloc_start, alloc_size);
        return ret;
    }

    /* Success!  Fill out the bookkeeping and we are done */
    out_block->platform_ctx = (void*)is_32bit;
    out_block->base_irq_id  = alloc_start;
    out_block->num_irq      = alloc_size;
    out_block->tgt_addr     = info.doorbell;
    out_block->tgt_data     = alloc_start;
    out_block->allocated    = true;
    return NO_ERROR;
}

void arm_gicv2m_free_msi_block(pcie_msi_block_t* block) {
    DEBUG_ASSERT(block);
    DEBUG_ASSERT(block->allocated);

    /* We stashed whether or not this came from the 32 bit pool in the platform context pointer */
    p2ra_state_t* pool = block->platform_ctx ? &g_32bit_targets : &g_64bit_targets;
    p2ra_free_range(pool, block->base_irq_id, block->num_irq);
    memset(block, 0, sizeof(*block));
}

void arm_gicv2m_register_msi_handler(const pcie_msi_block_t* block,
                                     uint                    msi_id,
                                     int_handler             handler,
                                     void*                   ctx) {
    DEBUG_ASSERT(block && block->allocated);
    DEBUG_ASSERT(msi_id < block->num_irq);
    register_int_handler(block->base_irq_id + msi_id, handler, ctx);
}

void arm_gicv2m_mask_unmask_msi(const pcie_msi_block_t* block,
                                uint                    msi_id,
                                bool                    mask) {
    DEBUG_ASSERT(block && block->allocated);
    DEBUG_ASSERT(msi_id < block->num_irq);
    if (mask)   mask_interrupt(block->base_irq_id + msi_id);
    else      unmask_interrupt(block->base_irq_id + msi_id);
}

#endif  // WITH_DEV_PCIE
