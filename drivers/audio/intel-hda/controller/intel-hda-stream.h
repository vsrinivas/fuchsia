// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/vmo.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include "drivers/audio/audio-proto/audio-proto.h"
#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "drivers/audio/intel-hda/utils/intel-hda-registers.h"

#include "thread-annotations.h"

namespace audio {
namespace intel_hda {

class IntelHDAStream : public mxtl::RefCounted<IntelHDAStream>,
                       public mxtl::WAVLTreeContainable<mxtl::RefPtr<IntelHDAStream>> {
public:
    using RefPtr = mxtl::RefPtr<IntelHDAStream>;
    using Tree   = mxtl::WAVLTree<uint16_t, RefPtr>;
    enum class Type { INVALID, INPUT, OUTPUT, BIDIR };

    union RequestBufferType {
        audio_proto::CmdHdr                 hdr;
        audio_proto::RingBufGetFifoDepthReq get_fifo_depth;
        audio_proto::RingBufGetBufferReq    get_buffer;
        audio_proto::RingBufStartReq        start;
        audio_proto::RingBufStopReq         stop;
    };

    // Hardware allows buffer descriptor lists (BDLs) to be up to 256
    // entries long.  With 30 maximum stream contexts, and 16 bytes per
    // entry, this works out to be about 123KB of RAM.  Pre-allocating this
    // amount of RAM which would almost certainly never get used seems like
    // a waste.  Limit the maximum desctiptor list length to 32 entries for
    // now.  This results in a worst case of just less than 16KB.  For a
    // system with 8 stream contexts (more typical) it works out to exactly
    // one 4k page.
    static constexpr size_t MAX_BDL_LENGTH = 32;
    static constexpr size_t MAX_STREAMS_PER_CONTROLLER = 30;

    // We carve our BDLs out of a contiguously allocated page aligned block
    // of memory.  Provided that the length of the chunks is a multiple of
    // 128 bytes, we can be certain that the start of all of our lists is on
    // a 128 byte boundary, as required by section 3.3.42
    static_assert(((sizeof(IntelHDABDLEntry) * MAX_BDL_LENGTH) % 128) == 0,
                  "All BDLs must be 128 byte aligned!");

    Type     type()            const { return type_; }
    Type     configured_type() const { return configured_type_; }
    uint8_t  tag()             const { return tag_; }
    uint16_t id()              const { return id_; }
    uint16_t GetKey()          const { return id(); }

    mx_status_t SetStreamFormat(uint16_t encoded_fmt,
                                const mxtl::RefPtr<DispatcherChannel>& channel)
        TA_EXCL(channel_lock_);
    void Deactivate() TA_EXCL(channel_lock_);
    void OnChannelClosed(const DispatcherChannel& channel) TA_EXCL(channel_lock_);

    mx_status_t ProcessClientRequest(DispatcherChannel* channel,
                                     const RequestBufferType& req,
                                     uint32_t req_size,
                                     mx::handle&& rxed_handle) TA_EXCL(channel_lock_);

    void ProcessStreamIRQ() TA_EXCL(notif_lock_);

private:
    friend class IntelHDAController;            // Only controller may construct us.
    friend class mxtl::RefPtr<IntelHDAStream>;  // Only our ref ptrs may destruct us.

    IntelHDAStream(Type                    type,
                   uint16_t                id,
                   hda_stream_desc_regs_t* regs,
                   mx_paddr_t              bdl_phys,
                   uintptr_t               bdl_virt);
    ~IntelHDAStream();

    void PrintDebugPrefix() const;

    void DeactivateLocked() TA_REQ(channel_lock_);
    void EnsureStoppedLocked() TA_REQ(channel_lock_) { EnsureStopped(regs_); }

    // Client request handlers
    mx_status_t ProcessGetFifoDepthLocked(const audio_proto::RingBufGetFifoDepthReq& req)
        TA_REQ(channel_lock_);
    mx_status_t ProcessGetBufferLocked(const audio_proto::RingBufGetBufferReq& req)
        TA_REQ(channel_lock_);
    mx_status_t ProcessStartLocked(const audio_proto::RingBufStartReq& req) TA_REQ(channel_lock_);
    mx_status_t ProcessStopLocked(const audio_proto::RingBufStopReq& req) TA_REQ(channel_lock_);

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

    // Paramters determined construction time.
    const Type                    type_       = Type::INVALID;
    const uint16_t                id_         = 0;
    hda_stream_desc_regs_t* const regs_       = nullptr;
    IntelHDABDLEntry*       const bdl_        = nullptr;
    const mx_paddr_t              bdl_phys_   = 0;

    // Parameters determined at allocation time.
    Type    configured_type_;
    uint8_t tag_;

    // The channel used by the application to talk to us once our format has
    // been set by the codec.
    mxtl::Mutex channel_lock_;
    mxtl::RefPtr<DispatcherChannel> channel_ TA_GUARDED(channel_lock_);
    mx::vmo ring_buffer_vmo_ TA_GUARDED(channel_lock_);

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
    mxtl::Mutex notif_lock_ TA_ACQ_AFTER(channel_lock_);
    mxtl::RefPtr<DispatcherChannel> irq_channel_ TA_GUARDED(notif_lock_);
};

}  // namespace intel_hda
}  // namespace audio
