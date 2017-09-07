// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memory_limit.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <iovec.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <fbl/algorithm.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

mx_status_t mem_limit_init(mem_limit_ctx_t* ctx) {
    if (!ctx) {
        return MX_ERR_INVALID_ARGS;
    }

    uint64_t limit = cmdline_get_uint64("kernel.memory-limit-mb", 0u);
    if (limit) {
        printf("Kernel memory limit of %zu MB found.\n", limit);
        ctx->memory_limit = limit * MB;
        ctx->found_kernel = 0;
        ctx->found_ramdisk = 0;
        return MX_OK;
    }

    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t mem_limit_get_iovs(mem_limit_ctx_t* ctx, uintptr_t range_base, size_t range_size,
                               iovec_t iovs[], size_t* used_cnt) {
    DEBUG_ASSERT(ctx);
    DEBUG_ASSERT(iovs);
    DEBUG_ASSERT(used_cnt);

    if (range_size == 0 || ctx->memory_limit == 0) {
        /* If our limit has been reached this range can be skipped */
        *used_cnt = 0;
        return MX_OK;
    }

    LTRACEF("scanning range %" PRIxPTR " of size %zu, (kernel start %#" PRIxPTR " limit %zu\n",
            range_base, range_size, ctx->kernel_base, ctx->memory_limit);
    // Convenience values for the offsets and sizes within the range.
    // These correspond to the two ranges that might be built to represent
    // a pair of ranges that correspond to a kernel and a ramdisk. They're
    // used instead of iovs[] directly to avoid casting for (void*) math.
    uintptr_t low_base, high_base = 0;
    size_t low_len, high_len = 0;

    /* This is where things get more complicated if we found the kernel_iov. On both
     * x86 and ARM the kernel and ramdisk will exist in the same memory range.
     * On x86 this is the lowmem region below 4GB based on where UEFI's page
     * allocations placed it. For ARM, it depends on the platform's bootrom, but
     * the important detail is that they both should be in the same contiguous
     * block of DRAM. Either way, we know the kernel + bss needs to be included
     * in memory regardless so that's the first priority.
     *
     * If we booted in the first place then we can assume we have enough space
     * for ourselves. k_low/k_high/r_high represent spans as follows:
     * |base|<k_low>[kernel]<k_high>[ramdisk]<r_high>[end]>
     *
     * Alternatively, if there is no ramdisk then the situation looks more like:
     * |base|<k_low>[kernel]<k_high>[end]
     *
     * TODO: when kernel relocation exists this will need to handle the ramdisk
     * being before the kernel_iov, as well as them possibly being in different
     * ranges.
     */
    uintptr_t k_base = ctx->kernel_base;
    size_t k_size = ctx->kernel_size;
    uintptr_t k_end = k_base + k_size;
    uintptr_t range_end = range_base + range_size;
    if (range_base <= k_base && k_base < range_end) {
        // First set up the kernel low/high for the spans we care about
        uint64_t k_low = k_base - range_base;
        uint64_t k_high = range_end;
        uint64_t tmp, r_high;
        low_base = k_base;
        low_len = k_size;
        ctx->memory_limit -= k_size;
        LTRACEF("kernel base %#" PRIxPTR " size %#" PRIxPTR "\n", k_base, k_size);

        // Add the ramdisk, but warn the user if we have to expand the limit to fit it in memory
        if (ctx->ramdisk_size && ctx->ramdisk_base >= range_base &&
                ctx->ramdisk_base + ctx->ramdisk_size <= range_end) {
            uintptr_t r_base = ctx->ramdisk_base;
            uintptr_t r_end = r_base + ctx->ramdisk_size;
            LTRACEF("ramdisk base %" PRIxPTR " size %" PRIxPTR "\n", r_base, ctx->ramdisk_size);
            tmp = fbl::min(ctx->memory_limit, ctx->ramdisk_size);
            if (tmp != ctx->ramdisk_size) {
                size_t diff = ctx->ramdisk_size - ctx->memory_limit;
                printf("WARNING: ramdisk forces the system to exceed the system memory limit"
                       "of %zu bytes by %zu bytes!\n", ctx->memory_limit, diff);
                ctx->memory_limit += diff;
                tmp = ctx->ramdisk_size;
            }
            high_base = r_base;
            high_len = tmp;
            ctx->memory_limit -= tmp;

            // If a ramdisk is found then the kernel ends at the ramdisk's base
            // rather than at the end of the range
            k_high = r_base - k_end;
            r_high = range_end - r_end;
            ctx->found_ramdisk = true;
        } else {
            // Set r_high to zero here so that the checks later to expand the
            // high vector work without any special casing.
            r_high = 0;
        }

        // We've created our kernel and ramdisk vecs, and now we expand them as
        // much as possible within the imposed limit, starting with the k_high
        // gap between the kernel and ramdisk_iov.
        tmp = fbl::min(ctx->memory_limit, k_high);
        if (tmp) {
            LTRACEF("growing low iov by %zu bytes.\n", tmp);
            low_len += tmp;
            ctx->memory_limit -= tmp;
        }

        // Handle space between the start of the range and the kernel base
        tmp = fbl::min(ctx->memory_limit, k_low);
        if (tmp) {
            low_base -= tmp;
            low_len += tmp;
            ctx->memory_limit -= tmp;
            LTRACEF("moving low iov base back by %zu to %#" PRIxPTR ".\n",
                    tmp, low_base);
        }

        // At this point we have already expanded the vector containing the
        // kernel as much as we can, so low_base + low_len either ends at the
        // start of the ramdisk, the end of the range, or the end of our memory
        // limit. If we still have any memory left that we're allowed to use and
        // there's space between the end of the ramdisk and end of the range,
        // then we can attempt to grow that the high vector by the difference.
        tmp = fbl::min(ctx->memory_limit, r_high);
        if (tmp) {
            LTRACEF("growing high iov by %zu bytes.\n", tmp);
            high_len += tmp;
            ctx->memory_limit -= tmp;
        }

        // Collapse the kernel and ramdisk into a single io vector if they're
        // adjacent to each other.
        if (low_base + low_len == high_base) {
            low_len += high_len;
            high_base = 0;
            high_len = 0;
            LTRACEF("Merging both iovs into a single iov base %#" PRIxPTR " size %zu\n",
                    low_base, low_len);
        }

        ctx->found_kernel = true;
    } else {
        // Set an adjusted local limit for the current range we're scanning
        // based on whether we have found the kernel and ramdisk yet. If we
        // haven't then we need to set aside space for them in future ranges by
        // restricting the space used by this range's vectors.
        size_t adjusted_limit = ctx->memory_limit;

        if (!ctx->found_kernel) {
            adjusted_limit -= fbl::min(ctx->kernel_size, adjusted_limit);
            if (ctx->ramdisk_size) {
                adjusted_limit -= fbl::min(ctx->ramdisk_size, adjusted_limit);
            }
        }

        LTRACEF("adjusted limit of %zu being used (found_kernel: %d, found_ramdisk: %d)\n", adjusted_limit, ctx->found_kernel, ctx->found_ramdisk);
        // No kernel here, presumably no ramdisk. Just add what we can.
        uint64_t tmp = fbl::min(adjusted_limit, range_size);
        low_base = range_base;
        low_len = tmp;
        ctx->memory_limit -= tmp;
        LTRACEF("using %zu bytes from base %#" PRIxPTR "\n", low_len, low_base);
    }

    DEBUG_ASSERT(low_base >= range_base);
    DEBUG_ASSERT(high_base == 0 || high_base >= range_base);
    DEBUG_ASSERT(low_base + low_len <= range_end);
    DEBUG_ASSERT(high_base + high_len <= range_end);
    DEBUG_ASSERT(low_len + high_len <= range_size);

    // Build the iovs with the ranges figured out above
    iovs[0].iov_base = reinterpret_cast<void*>(low_base);
    iovs[0].iov_len = ROUNDUP_PAGE_SIZE(low_len);
    iovs[1].iov_base = reinterpret_cast<void*>(high_base);
    iovs[1].iov_len = ROUNDUP_PAGE_SIZE(high_len);

    // Set the count to 0 through 2 depending on vectors used
    *used_cnt = !!(iovs[0].iov_len) + !!(iovs[1].iov_len);

    LTRACEF("used %zu iov%s remaining memory %zu bytes\n", *used_cnt, (*used_cnt == 1) ? "," : "s,", ctx->memory_limit);
    return MX_OK;
}

mx_status_t mem_limit_add_arenas_from_range(mem_limit_ctx_t* ctx, uintptr_t range_base,
                                            size_t range_size, pmm_arena_info_t arena_template) {
    size_t used;
    iovec_t vecs[2];
    mx_status_t status = mem_limit_get_iovs(ctx, range_base, range_size, vecs, &used);

    if (status != MX_OK) {
        return status;
    }

    // Use the arena template and add any we created from this range to the pmm
    for (size_t i = 0; i < used; i++) {
        auto arena = arena_template;
        arena.base = reinterpret_cast<paddr_t>(vecs[i].iov_base);
        arena.size = vecs[i].iov_len;

        status = pmm_add_arena(&arena);

        // If either vector failed then abort the rest of the operation. There is no
        // valid situation where only the second vector is used.
        if (status != MX_OK) {
            break;
        }
    }

    return status;
}
