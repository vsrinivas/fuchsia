// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vmo_mapper.h>

#include <audio-proto/audio-proto.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <intel-hda/utils/intel-hda-registers.h>

#include "pinned-vmo.h"
#include "debug-logging.h"
#include "thread-annotations.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

class IntelHDACodec;

class IntelHDAStream : public fbl::RefCounted<IntelHDAStream>,
                       public fbl::WAVLTreeContainable<fbl::RefPtr<IntelHDAStream>> {
public:
    using RefPtr = fbl::RefPtr<IntelHDAStream>;
    using Tree   = fbl::WAVLTree<uint16_t, RefPtr>;
    enum class Type { INVALID, INPUT, OUTPUT, BIDIR };

    // Hardware allows buffer descriptor lists (BDLs) to be up to 256
    // entries long.
    static constexpr size_t MAX_BDL_LENGTH = 256;

    static fbl::RefPtr<IntelHDAStream> Create(
        Type type,
        uint16_t id,
        hda_stream_desc_regs_t* regs,
        const fbl::RefPtr<RefCountedBti>& pci_bti);

    const char* log_prefix()      const { return log_prefix_; }
    Type        type()            const { return type_; }
    Type        configured_type() const { return configured_type_; }
    uint8_t     tag()             const { return tag_; }
    uint16_t    id()              const { return id_; }
    uint16_t    GetKey()          const { return id(); }

    zx_status_t SetStreamFormat(const fbl::RefPtr<dispatcher::ExecutionDomain>& domain,
                                uint16_t encoded_fmt,
                                zx::channel* client_endpoint_out) TA_EXCL(channel_lock_);
    void Deactivate() TA_EXCL(channel_lock_);

    void ProcessStreamIRQ() TA_EXCL(notif_lock_);

private:
    friend class IntelHDAController;  // Controllers have access to stuff like Reset and Configure
    friend class fbl::RefPtr<IntelHDAStream>;  // Only our ref ptrs may destruct us.

    IntelHDAStream(Type type,
                   uint16_t id,
                   hda_stream_desc_regs_t* regs,
                   const fbl::RefPtr<RefCountedBti>& pci_bti);
    ~IntelHDAStream();

    zx_status_t Initialize();

    void DeactivateLocked() TA_REQ(channel_lock_);
    void EnsureStoppedLocked() TA_REQ(channel_lock_) { EnsureStopped(regs_); }

    // Client request handlers
    zx_status_t ProcessClientRequest(dispatcher::Channel* channel) TA_EXCL(channel_lock_);
    void ProcessClientDeactivate(const dispatcher::Channel* channel) TA_EXCL(channel_lock_);
    zx_status_t ProcessGetFifoDepthLocked(const audio_proto::RingBufGetFifoDepthReq& req)
        TA_REQ(channel_lock_);
    zx_status_t ProcessGetBufferLocked(const audio_proto::RingBufGetBufferReq& req)
        TA_REQ(channel_lock_);
    zx_status_t ProcessStartLocked(const audio_proto::RingBufStartReq& req) TA_REQ(channel_lock_);
    zx_status_t ProcessStopLocked(const audio_proto::RingBufStopReq& req) TA_REQ(channel_lock_);

    // Release the client ring buffer (if one has been assigned)
    void ReleaseRingBufferLocked() TA_REQ(channel_lock_);

    // Enter and exit the HW reset state.
    //
    // TODO(johngro) : leaving streams in reset at all times seems to have
    // trouble with locking up the hardware (it becomes completely unresponsive
    // to reset, both stream reset and top level reset).  One day we should
    // figure out why; in the meantime, do not leave streams held in reset for
    // any length of time.
    void Reset() { Reset(regs_); }

    // Called during stream allocation and release to configure the type of
    // stream (in the case of a bi-directional stream) and the tag that the
    // stream will put into the outbound SDO frames.
    void Configure(Type type, uint8_t tag);

    // Static helpers which can be used during early initialization
    static void EnsureStopped(hda_stream_desc_regs_t* regs);
    static void Reset(hda_stream_desc_regs_t* regs);

    // Accessor for the CPU accessble view of the Buffer Descriptor List
    IntelHDABDLEntry* bdl() const {
        return reinterpret_cast<IntelHDABDLEntry*>(bdl_cpu_mem_.start());
    }

    // Paramters determined construction time.
    const Type                    type_ = Type::INVALID;
    const uint16_t                id_   = 0;
    hda_stream_desc_regs_t* const regs_ = nullptr;

    // Parameters determined at allocation time.
    Type    configured_type_;
    uint8_t tag_;

    // Log prefix storage
    char log_prefix_[LOG_PREFIX_STORAGE] = { 0 };

    // A reference to our controller's BTI.  We will need to this to grant the
    // controller access to the BDLs and the ring buffers that this stream needs
    // to operate.
    const fbl::RefPtr<RefCountedBti> pci_bti_;

    // Storage allocated for this stream context's buffer descriptor list.
    fbl::VmoMapper bdl_cpu_mem_;
    PinnedVmo      bdl_hda_mem_;

    // The channel used by the application to talk to us once our format has
    // been set by the codec.
    fbl::Mutex channel_lock_;
    fbl::RefPtr<dispatcher::Channel> channel_ TA_GUARDED(channel_lock_);
    PinnedVmo pinned_ring_buffer_ TA_GUARDED(channel_lock_);

    // Paramters determined after stream format configuration.
    uint16_t encoded_fmt_ = 0;
    uint16_t fifo_depth_ = 0;
    uint32_t bytes_per_frame_ TA_GUARDED(channel_lock_) = 0;

    // Paramters determined after ring buffer allocation.
    uint32_t cyclic_buffer_length_ TA_GUARDED(channel_lock_) = 0;
    uint32_t bdl_last_valid_index_ TA_GUARDED(channel_lock_) = 0;

    // Start/stop flag.
    bool running_ TA_GUARDED(channel_lock_) = false;

    // State used by the IRQ thread to deliver position update notifications.
    fbl::Mutex notif_lock_ TA_ACQ_AFTER(channel_lock_);
    fbl::RefPtr<dispatcher::Channel> irq_channel_ TA_GUARDED(notif_lock_);
};

}  // namespace intel_hda
}  // namespace audio
