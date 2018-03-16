// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <limits.h>
#include <zircon/types.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vmar_manager.h>

#include <dispatcher-pool/dispatcher-channel.h>

namespace audio {
namespace intel_hda {

// Constants

// HDA controllers can have at most 30 stream contexts.
constexpr size_t MAX_STREAMS_PER_CONTROLLER = 30;

// CORB/RIRB should take no more than a page
constexpr size_t MAPPED_CORB_RIRB_SIZE = PAGE_SIZE;

// Individual BDLs should be 1 page each
constexpr size_t MAPPED_BDL_SIZE = PAGE_SIZE;

#define _SIC_ static inline constexpr
template <typename T> _SIC_ T  OR(T x, T y) { return static_cast<T>(x | y); }
template <typename T> _SIC_ T AND(T x, T y) { return static_cast<T>(x & y); }
#undef _SIC_

using WaitConditionFn = bool (*)(void*);
zx_status_t WaitCondition(zx_time_t timeout,
                          zx_time_t poll_interval,
                          WaitConditionFn cond,
                          void* cond_ctx);

// Static container for the driver wide VMARs that we stash all of our register
// mappings in, in order to make efficient use of kernel PTEs
class DriverVmars {
  public:
    static zx_status_t Initialize();
    static void Shutdown();
    static const fbl::RefPtr<fbl::VmarManager>& registers() { return registers_; }
  private:
    static fbl::RefPtr<fbl::VmarManager> registers_;
};

// Utility class which manages a Bus Transaction Initiator using RefPtrs
// (allowing the BTI to be shared by multiple objects)
class RefCountedBti : public fbl::RefCounted<RefCountedBti> {
  public:
    static fbl::RefPtr<RefCountedBti> Create(zx::bti initiator);
    const zx::bti& initiator() const { return initiator_; }

  private:
    explicit RefCountedBti(zx::bti initiator) : initiator_(fbl::move(initiator)) { }
    zx::bti initiator_;
};

struct StreamFormat {
    // Stream format bitfields documented in section 3.7.1
    static constexpr uint16_t FLAG_NON_PCM = (1u << 15);

    constexpr StreamFormat() { }
    explicit constexpr StreamFormat(uint16_t raw_data) : raw_data_(raw_data) { }

    uint32_t BASE() const { return (raw_data_ & (1u << 14)) ? 44100 : 48000; }
    uint32_t CHAN() const { return (raw_data_ & 0xF) + 1; }
    uint32_t DIV()  const { return ((raw_data_ >> 8) & 0x7) + 1; }
    uint32_t MULT() const {
        uint32_t bits = (raw_data_ >> 11) & 0x7;
        if (bits >= 4)
            return 0;
        return bits + 1;
    }
    uint32_t BITS_NDX() const { return (raw_data_ >> 4) & 0x7; }
    uint32_t BITS() const {
        switch (BITS_NDX()) {
        case 0: return 8u;
        case 1: return 16u;
        case 2: return 20u;
        case 3: return 24u;
        case 4: return 32u;
        default: return 0u;
        }
    }

    bool     is_pcm()        const { return (raw_data_ & FLAG_NON_PCM) == 0; }
    uint32_t sample_rate()   const { return (BASE() * MULT()) / DIV(); }
    uint32_t channels()      const { return CHAN(); }
    uint32_t bits_per_chan() const { return BITS(); }

    uint32_t bytes_per_frame() const {
        uint32_t ret = CHAN();
        switch (BITS_NDX()) {
        case 0: return ret;
        case 1: return ret << 1;
        case 2:
        case 3:
        case 4: return ret << 2;
        default: return 0u;
        }

    }

    bool SanityCheck() const {
        if (raw_data_ == 0x8000)
            return true;

        if (raw_data_ & 0x8080)
            return false;

        return (BITS() && MULT());
    }

    uint16_t raw_data_ = 0;
};

// Boilerplate code to handle an IOCTL request to create a channel from an
// application.  Assuming that the request passes all of the sanity checks,
// attempts to create a channel and bind it to this owner using the supplied
// dispatching behavior, then send the other end of the channel back to the
// application.
zx_status_t HandleDeviceIoctl(uint32_t op,
                              void* out_buf,
                              size_t out_len,
                              size_t* out_actual,
                              const fbl::RefPtr<dispatcher::ExecutionDomain>& domain,
                              dispatcher::Channel::ProcessHandler phandler,
                              dispatcher::Channel::ChannelClosedHandler chandler);


// Attempts to create and activate a channel using the supplied dispatcher
// bindings and binding it to this ExeuctionDomain in the process.  Callers must
// take ownership of the remote channel endpoint, but may choose to ignore the
// local channel endpoint by passing nullptr for local_endpoint_out.  Upon
// success, a reference to the created dispatcher::Channel will be held by the
// channel's ExeuctionDomain (as a result of the activation operation)
zx_status_t CreateAndActivateChannel(const fbl::RefPtr<dispatcher::ExecutionDomain>& domain,
                                     dispatcher::Channel::ProcessHandler phandler,
                                     dispatcher::Channel::ChannelClosedHandler chandler,
                                     fbl::RefPtr<dispatcher::Channel>* local_endpoint_out,
                                     zx::channel* remote_endpoint_out);

}  // namespace intel_hda
}  // namespace audio
