// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/device/intel-hda.h>
#include <zircon/process.h>
#include <zx/channel.h>
#include <fbl/algorithm.h>
#include <string.h>

#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
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

zx_status_t WaitCondition(zx_time_t timeout,
                          zx_time_t poll_interval,
                          WaitConditionFn cond,
                          void* cond_ctx) {
    ZX_DEBUG_ASSERT(poll_interval != ZX_TIME_INFINITE);
    ZX_DEBUG_ASSERT(cond != nullptr);

    zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    timeout += now;

    while (!cond(cond_ctx)) {
        now = zx_clock_get(ZX_CLOCK_MONOTONIC);
        if (now >= timeout)
            return ZX_ERR_TIMED_OUT;

        zx_time_t sleep_time = timeout - now;
        if (poll_interval < sleep_time)
            sleep_time = poll_interval;

        zx_nanosleep(zx_deadline_after(sleep_time));
    }

    return ZX_OK;
}

zx_status_t GetVMORegionInfo(const zx::vmo& vmo,
                             uint64_t       vmo_size,
                             VMORegion*     regions_out,
                             uint32_t*      num_regions_inout) {
    zx_status_t res;

    if ((!vmo.is_valid())               ||
        (regions_out        == nullptr) ||
        (num_regions_inout  == nullptr) ||
        (*num_regions_inout == 0))
        return ZX_ERR_INVALID_ARGS;

    // Defaults
    uint32_t num_regions = *num_regions_inout;
    *num_regions_inout = 0;
    memset(regions_out, 0, sizeof(*regions_out) * num_regions);

    constexpr size_t   PAGES_PER_VMO_OP = 32;   // 256 bytes on the stack
    constexpr uint64_t BYTES_PER_VMO_OP = PAGES_PER_VMO_OP << IHDA_PAGE_SHIFT;

    zx_paddr_t page_addrs[PAGES_PER_VMO_OP];
    uint64_t offset = 0;
    uint32_t region = 0;

    while ((offset < vmo_size) && (region < num_regions)) {
        uint64_t todo = fbl::min(vmo_size - offset, BYTES_PER_VMO_OP);
        uint32_t todo_pages = static_cast<uint32_t>((todo + IHDA_PAGE_MASK) >> IHDA_PAGE_SHIFT);

        memset(page_addrs, 0, sizeof(page_addrs));
        res = vmo.op_range(ZX_VMO_OP_LOOKUP,
                           offset, todo,
                           &page_addrs, sizeof(page_addrs[0]) * todo_pages);
        if (res != ZX_OK)
            return res;

        for (uint32_t i = 0; (i < todo_pages) && (region < num_regions); ++i) {
            // Physical addresses must be page aligned and may not be 0.
            if ((page_addrs[i] & IHDA_PAGE_MASK) || (page_addrs[i] == 0))
                return ZX_ERR_INTERNAL;

            bool     merged = false;
            uint64_t region_size = fbl::min(vmo_size - offset, IHDA_PAGE_SIZE);

            if (region > 0) {
                auto& prev = regions_out[region - 1];
                zx_paddr_t prev_end = prev.phys_addr + prev.size;

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
                ZX_DEBUG_ASSERT(region < num_regions);
                regions_out[region].phys_addr = page_addrs[i];
                regions_out[region].size      = region_size;
                region++;
            }

            offset += region_size;
        }
    }

    if (offset < vmo_size)
        return ZX_ERR_BUFFER_TOO_SMALL;

    *num_regions_inout = region;

    return ZX_OK;
}

zx_status_t ContigPhysMem::Allocate(size_t size) {
    static_assert(fbl::is_pow2(IHDA_PAGE_SIZE),
                  "In what universe is your page size not a power of 2?  Seriously!?");

    if (!size)
        return ZX_ERR_INVALID_ARGS;

    if (vmo_.is_valid())
        return ZX_ERR_BAD_STATE;

    ZX_DEBUG_ASSERT(!size_);
    ZX_DEBUG_ASSERT(!actual_size_);
    ZX_DEBUG_ASSERT(!virt_);
    ZX_DEBUG_ASSERT(!phys_);

    size_ = size;
    actual_size_ = fbl::round_up(size_, IHDA_PAGE_SIZE);

    // Allocate a page aligned contiguous buffer.
    zx::vmo     vmo;
    zx_status_t res;

    res = zx_vmo_create_contiguous(get_root_resource(),
                                   actual_size(),
                                   0,
                                   vmo.reset_and_get_address());
    if (res != ZX_OK)
        goto finished;

    // Now fetch its physical address, so we can tell hardware about it.
    res = vmo.op_range(ZX_VMO_OP_LOOKUP, 0,
                       fbl::min(actual_size(), IHDA_PAGE_SIZE),
                       &phys_, sizeof(phys_));

finished:
    if (res != ZX_OK) {
        phys_ = 0;
        size_ = 0;
        actual_size_ = 0;
    } else {
        vmo_ = fbl::move(vmo);
    }

    return res;
}

zx_status_t ContigPhysMem::Map() {
    if (!vmo_.is_valid() || (virt_ != 0))
        return ZX_ERR_BAD_STATE;

    ZX_DEBUG_ASSERT(size_);
    ZX_DEBUG_ASSERT(actual_size_);

    // TODO(johngro) : How do I specify the cache policy for this mapping?
    ZX_DEBUG_ASSERT(virt_ == 0);
    zx_status_t res = zx_vmar_map(zx_vmar_root_self(), 0u,
                                  vmo_.get(), 0u,
                                  actual_size_,
                                  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                  &virt_);

    ZX_DEBUG_ASSERT((res == ZX_OK) == (virt_ != 0u));
    return res;
}

void ContigPhysMem::Release() {
    if (virt_ != 0) {
        ZX_DEBUG_ASSERT(actual_size_ != 0);
        zx_vmar_unmap(zx_vmar_root_self(), virt_, actual_size_);
        virt_ = 0;
    }

    vmo_.reset();
    phys_ = 0;
    size_ = 0;
    actual_size_ = 0;
}

zx_status_t HandleDeviceIoctl(uint32_t op,
                              void* out_buf,
                              size_t out_len,
                              size_t* out_actual,
                              const fbl::RefPtr<dispatcher::ExecutionDomain>& domain,
                              dispatcher::Channel::ProcessHandler phandler,
                              dispatcher::Channel::ChannelClosedHandler chandler) {
    if (op != IHDA_IOCTL_GET_CHANNEL) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if ((out_buf == nullptr) ||
        (out_actual == nullptr) ||
        (out_len != sizeof(zx_handle_t))) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx::channel remote_endpoint_out;
    zx_status_t res = CreateAndActivateChannel(domain,
                                               fbl::move(phandler),
                                               fbl::move(chandler),
                                               nullptr,
                                               &remote_endpoint_out);
    if (res == ZX_OK) {
        *(reinterpret_cast<zx_handle_t*>(out_buf)) = remote_endpoint_out.release();
        *out_actual = sizeof(zx_handle_t);
    }

    return res;
}

zx_status_t CreateAndActivateChannel(const fbl::RefPtr<dispatcher::ExecutionDomain>& domain,
                                     dispatcher::Channel::ProcessHandler phandler,
                                     dispatcher::Channel::ChannelClosedHandler chandler,
                                     fbl::RefPtr<dispatcher::Channel>* local_endpoint_out,
                                     zx::channel* remote_endpoint_out) {
    if (remote_endpoint_out == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto channel = dispatcher::Channel::Create();
    if (channel == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t res = channel->Activate(remote_endpoint_out,
                                        domain,
                                        fbl::move(phandler),
                                        fbl::move(chandler));
    if ((res == ZX_OK) && (local_endpoint_out != nullptr)) {
        *local_endpoint_out = fbl::move(channel);
    }

    return res;
}

}  // namespace intel_hda
}  // namespace audio
