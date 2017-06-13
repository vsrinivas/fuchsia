// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <mx/handle.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_ptr.h>

#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "drivers/audio/intel-hda/utils/codec-commands.h"
#include "drivers/audio/intel-hda/utils/intel-hda-proto.h"

namespace audio {
namespace intel_hda {
namespace codecs {

class IntelHDAStreamBase;

class IntelHDACodecDriverBase : public DispatcherChannel::Owner {
public:
    virtual void Shutdown();

    // Properties
    mx_device_t* codec_device() const { return codec_device_; }
    mx_time_t    create_time()  const { return create_time_; }

    // Unsolicited tag allocation for streams
    mx_status_t AllocateUnsolTag(const IntelHDAStreamBase& stream, uint8_t* out_tag);
    void ReleaseUnsolTag(const IntelHDAStreamBase& stream, uint8_t tag);
    void ReleaseAllUnsolTags(const IntelHDAStreamBase& stream);

protected:
    static constexpr uint32_t CODEC_TID = 0xFFFFFFFF;

    virtual ~IntelHDACodecDriverBase() { }

    ///////////////////////////////////////////////////////////////////////////
    //
    // Methods used by derived classes in order to implement their driver.
    //
    ///////////////////////////////////////////////////////////////////////////

    // Bind should only ever be called exactly once (during driver
    // instantiation).  Drivers must make sure that no other methods are in
    // flight during a call to Bind.
    mx_status_t Bind(mx_device_t* codec_dev);

    // Send a codec command to our codec device.
    mx_status_t SendCodecCommand(uint16_t nid, CodecVerb verb, bool no_ack);

    virtual mx_status_t Start() { return MX_OK; }
    virtual mx_status_t ProcessUnsolicitedResponse(const CodecResponse& resp) { return MX_OK; }
    virtual mx_status_t ProcessSolicitedResponse  (const CodecResponse& resp) { return MX_OK; }

    // DispatcherChannel::Owner implementation
    mx_status_t ProcessChannel(DispatcherChannel* channel) final;
    void NotifyChannelDeactivated(const DispatcherChannel& channel) final;

    // Unsolicited tag allocation for codecs.
    mx_status_t AllocateUnsolTag(uint8_t* out_tag) { return AllocateUnsolTag(CODEC_TID, out_tag); }
    void ReleaseUnsolTag(uint8_t tag) { ReleaseUnsolTag(CODEC_TID, tag); }

    mxtl::RefPtr<IntelHDAStreamBase> GetActiveStream(uint32_t stream_id)
        __TA_EXCLUDES(active_streams_lock_);
    mx_status_t ActivateStream(const mxtl::RefPtr<IntelHDAStreamBase>& stream)
        __TA_EXCLUDES(active_streams_lock_);
    mx_status_t DeactivateStream(uint32_t stream_id)
        __TA_EXCLUDES(active_streams_lock_);

    // Debug logging
    virtual void PrintDebugPrefix() const;

private:
    friend class mxtl::RefPtr<IntelHDACodecDriverBase>;

    union CodecChannelResponses {
        ihda_proto::CmdHdr            hdr;
        ihda_proto::SendCORBCmdResp   send_corb;
        ihda_proto::RequestStreamResp request_stream;
        ihda_proto::SetStreamFmtResp  set_stream_fmt;
    };

    // Unsolicited response tag to stream ID bookkeeping.
    mx_status_t AllocateUnsolTag(uint32_t stream_id, uint8_t* out_tag);
    void        ReleaseUnsolTag (uint32_t stream_id, uint8_t  tag);
    void        ReleaseAllUnsolTags(uint32_t stream_id);
    mx_status_t MapUnsolTagToStreamId(uint8_t tag, uint32_t* out_stream_id);

    // Called in order to unlink this device from the controller driver.  After
    // this call returns, the codec driver is guaranteed that no calls to any of
    // the driver implemented callbacks (see below) are in flight, and that no
    // new calls will be initiated.  It is not safe to make this call during a
    // controller callback.  To unlink from a controller during a callback,
    // return an error code from the callback.
    void UnlinkFromController();

    mx_status_t ProcessStreamResponse(const mxtl::RefPtr<IntelHDAStreamBase>& stream,
                                      const CodecChannelResponses& resp,
                                      uint32_t resp_size,
                                      mx::handle&& rxed_handle);

    mx_device_t* codec_device_ = nullptr;
    mx_time_t    create_time_  = mx_time_get(MX_CLOCK_MONOTONIC);

    mxtl::Mutex device_channel_lock_;
    mxtl::RefPtr<DispatcherChannel> device_channel_ __TA_GUARDED(device_channel_lock_);

    using ActiveStreams = mxtl::WAVLTree<uint32_t, mxtl::RefPtr<IntelHDAStreamBase>>;
    mxtl::Mutex   active_streams_lock_;
    ActiveStreams active_streams_ __TA_GUARDED(active_streams_lock_);

    mxtl::Mutex shutdown_lock_ __TA_ACQUIRED_BEFORE(device_channel_lock_, active_streams_lock_);
    bool        shutting_down_ __TA_GUARDED(shutdown_lock_) = false;

    // State for tracking unsolicited response tag allocations.
    //
    // Note: If we wanted to save a bit of RAM, we could move this to a
    // dynamically allocated list/tree based system.  For now, however, this LUT
    // is dirt simple and does the job.
    mxtl::Mutex unsol_tag_lock_;
    uint64_t free_unsol_tags_ __TA_GUARDED(unsol_tag_lock_) = 0xFFFFFFFFFFFFFFFEu;
    uint32_t unsol_tag_to_stream_id_map_[sizeof(free_unsol_tags_) << 3]
        __TA_GUARDED(unsol_tag_lock_);
};

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
