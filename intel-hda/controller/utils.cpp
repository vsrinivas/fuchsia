// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//TODO: stop using dev->props and dev->prop_count
#define DDK_INTERNAL

#include <magenta/assert.h>
#include <ddk/binding.h>
#include <magenta/process.h>
#include <mxtl/algorithm.h>
#include <string.h>

#include "utils.h"

namespace audio {
namespace intel_hda {

// TODO(johngro) : Don't define this here.  Fetch this information from the
// system using a syscall when we can.
static constexpr uint32_t IHDA_PAGE_SHIFT = 12;
static constexpr size_t   IHDA_PAGE_SIZE = static_cast<size_t>(1) << IHDA_PAGE_SHIFT;
static constexpr size_t   IHDA_PAGE_MASK = IHDA_PAGE_SIZE - 1;

#ifdef PAGE_SIZE
static_assert(IHDA_PAGE_SIZE == PAGE_SIZE, "PAGE_SIZE assumption mismatch!!");
#endif  // PAGE_SIZE

namespace {
mx_status_t GetDevProperty(const mx_device_t* dev, uint16_t prop_id, uint32_t* out) {
    if (!out)         return ERR_INVALID_ARGS;
    if (!dev)         return ERR_INVALID_ARGS;
    if (!dev->props)  return ERR_NOT_FOUND;

    MX_DEBUG_ASSERT(out);

    for (uint32_t i = 0; i < dev->prop_count; ++i) {
        if (dev->props[i].id == prop_id) {
            *out = dev->props[i].value;
            return NO_ERROR;
        }
    }

    return ERR_NOT_FOUND;
}
}

template <typename T>
mx_status_t GetDevProperty(const mx_device_t* dev, uint16_t prop_id, T* out) {
    uint32_t    val;
    mx_status_t res;

    if ((res = GetDevProperty(dev, prop_id, &val)) == NO_ERROR)
        *out = static_cast<T>(val);

    return res;
}

template mx_status_t GetDevProperty<uint32_t>(const mx_device_t* dev, uint16_t id, uint32_t* out);
template mx_status_t GetDevProperty<uint16_t>(const mx_device_t* dev, uint16_t id, uint16_t* out);
template mx_status_t GetDevProperty<uint8_t> (const mx_device_t* dev, uint16_t id, uint8_t*  out);

mx_status_t WaitCondition(mx_time_t timeout,
                          mx_time_t poll_interval,
                          WaitConditionFn cond,
                          void* cond_ctx) {
    MX_DEBUG_ASSERT(poll_interval != MX_TIME_INFINITE);
    MX_DEBUG_ASSERT(cond != nullptr);

    mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
    timeout += now;

    while (!cond(cond_ctx)) {
        now = mx_time_get(MX_CLOCK_MONOTONIC);
        if (now >= timeout)
            return ERR_TIMED_OUT;

        mx_time_t sleep_time = timeout - now;
        if (poll_interval < sleep_time)
            sleep_time = poll_interval;

        mx_nanosleep(mx_deadline_after(sleep_time));
    }

    return NO_ERROR;
}

mx_status_t GetVMORegionInfo(const mx::vmo& vmo,
                             uint64_t       vmo_size,
                             VMORegion*     regions_out,
                             uint32_t*      num_regions_inout) {
    mx_status_t res;

    if ((!vmo.is_valid())               ||
        (regions_out        == nullptr) ||
        (num_regions_inout  == nullptr) ||
        (*num_regions_inout == 0))
        return ERR_INVALID_ARGS;

    // Defaults
    uint32_t num_regions = *num_regions_inout;
    *num_regions_inout = 0;
    memset(regions_out, 0, sizeof(*regions_out) * num_regions);

    constexpr size_t   PAGES_PER_VMO_OP = 32;   // 256 bytes on the stack
    constexpr uint64_t BYTES_PER_VMO_OP = PAGES_PER_VMO_OP << IHDA_PAGE_SHIFT;

    mx_paddr_t page_addrs[PAGES_PER_VMO_OP];
    uint64_t offset = 0;
    uint32_t region = 0;

    while ((offset < vmo_size) && (region < num_regions)) {
        uint64_t todo = mxtl::min(vmo_size - offset, BYTES_PER_VMO_OP);
        uint32_t todo_pages = static_cast<uint32_t>((todo + IHDA_PAGE_MASK) >> IHDA_PAGE_SHIFT);

        memset(page_addrs, 0, sizeof(page_addrs));
        res = vmo.op_range(MX_VMO_OP_LOOKUP,
                           offset, todo,
                           &page_addrs, sizeof(page_addrs[0]) * todo_pages);
        if (res != NO_ERROR)
            return res;

        for (uint32_t i = 0; (i < todo_pages) && (region < num_regions); ++i) {
            // Physical addresses must be page aligned and may not be 0.
            if ((page_addrs[i] & IHDA_PAGE_MASK) || (page_addrs[i] == 0))
                return ERR_INTERNAL;

            bool     merged = false;
            uint64_t region_size = mxtl::min(vmo_size - offset, IHDA_PAGE_SIZE);

            if (region > 0) {
                auto& prev = regions_out[region - 1];
                mx_paddr_t prev_end = prev.phys_addr + prev.size;

                if (prev_end == page_addrs[i]) {
                    // The end of the previous region and the start of this one match.
                    // Merge them by bumping the previous region's size by a page.
                    prev.size += region_size;
                    merged = true;
                }
            }

            if (!merged) {
                // The regions do not line up or there was no previous region.
                // Start a new one.
                MX_DEBUG_ASSERT(region < num_regions);
                regions_out[region].phys_addr = page_addrs[i];
                regions_out[region].size      = region_size;
                region++;
            }

            offset += region_size;
        }
    }

    if (offset < vmo_size)
        return ERR_BUFFER_TOO_SMALL;

    *num_regions_inout = region;

    return NO_ERROR;
}

mx_status_t ContigPhysMem::Allocate(size_t size) {
    static_assert(mxtl::is_pow2(IHDA_PAGE_SIZE),
                  "In what universe is your page size not a power of 2?  Seriously!?");

    if (!size)
        return ERR_INVALID_ARGS;

    if (vmo_.is_valid())
        return ERR_BAD_STATE;

    MX_DEBUG_ASSERT(!size_);
    MX_DEBUG_ASSERT(!actual_size_);
    MX_DEBUG_ASSERT(!virt_);
    MX_DEBUG_ASSERT(!phys_);

    size_ = size;
    actual_size_ = mxtl::roundup(size_, IHDA_PAGE_SIZE);

    // Allocate a page aligned contiguous buffer.
    mx::vmo     vmo;
    mx_status_t res;

    res = mx_vmo_create_contiguous(get_root_resource(), actual_size(), 0, vmo.get_address());
    if (res != NO_ERROR)
        goto finished;

    // Now fetch its physical address, so we can tell hardware about it.
    res = vmo.op_range(MX_VMO_OP_LOOKUP, 0,
                       mxtl::min(actual_size(), IHDA_PAGE_SIZE),
                       &phys_, sizeof(phys_));

finished:
    if (res != NO_ERROR) {
        phys_ = 0;
        size_ = 0;
        actual_size_ = 0;
    } else {
        vmo_ = mxtl::move(vmo);
    }

    return res;
}

mx_status_t ContigPhysMem::Map() {
    if (!vmo_.is_valid() || (virt_ != 0))
        return ERR_BAD_STATE;

    MX_DEBUG_ASSERT(size_);
    MX_DEBUG_ASSERT(actual_size_);

    // TODO(johngro) : How do I specify the cache policy for this mapping?
    MX_DEBUG_ASSERT(virt_ == 0);
    mx_status_t res = mx_vmar_map(mx_vmar_root_self(), 0u,
                                  vmo_.get(), 0u,
                                  actual_size_,
                                  MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                                  &virt_);

    MX_DEBUG_ASSERT((res == NO_ERROR) == (virt_ != 0u));
    return res;
}

void ContigPhysMem::Release() {
    if (virt_ != 0) {
        MX_DEBUG_ASSERT(actual_size_ != 0);
        mx_vmar_unmap(mx_vmar_root_self(), virt_, actual_size_);
        virt_ = 0;
    }

    vmo_.reset();
    phys_ = 0;
    size_ = 0;
    actual_size_ = 0;
}

}  // namespace intel_hda
}  // namespace audio
