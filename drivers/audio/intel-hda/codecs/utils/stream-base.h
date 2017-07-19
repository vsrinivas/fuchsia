// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <mx/channel.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>

#include "drivers/audio/audio-proto/audio-proto.h"
#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "drivers/audio/intel-hda/utils/codec-commands.h"
#include "drivers/audio/intel-hda/utils/intel-hda-proto.h"

namespace audio {
namespace intel_hda {
namespace codecs {

class IntelHDACodecDriverBase;

class IntelHDAStreamBase : public DispatcherChannel::Owner,
                           public mxtl::WAVLTreeContainable<mxtl::RefPtr<IntelHDAStreamBase>> {
public:
    mx_status_t Activate(mxtl::RefPtr<IntelHDACodecDriverBase>&& parent_codec,
                         const mxtl::RefPtr<DispatcherChannel>& codec_channel)
        __TA_EXCLUDES(obj_lock_);

    void Deactivate() __TA_EXCLUDES(obj_lock_);

    mx_status_t ProcessResponse(const CodecResponse& resp) __TA_EXCLUDES(obj_lock_);
    mx_status_t ProcessRequestStream(const ihda_proto::RequestStreamResp& resp)
        __TA_EXCLUDES(obj_lock_);
    mx_status_t ProcessSetStreamFmt(const ihda_proto::SetStreamFmtResp& resp,
                                    mx::channel&& ring_buffer_channel) __TA_EXCLUDES(obj_lock_);

    uint32_t id()       const { return id_; }
    bool     is_input() const { return is_input_; }
    uint32_t GetKey()   const { return id(); }

protected:
    friend class mxtl::RefPtr<IntelHDAStreamBase>;

    enum class Ack {
        NO,
        YES,
    };

    IntelHDAStreamBase(uint32_t id, bool is_input);
    virtual ~IntelHDAStreamBase();

    // Properties available to subclasses.
    uint8_t dma_stream_tag() const __TA_REQUIRES(obj_lock_) {
        return dma_stream_tag_;
    }

    const mxtl::RefPtr<IntelHDACodecDriverBase>& parent_codec() const __TA_REQUIRES(obj_lock_) {
        return parent_codec_;
    }

    bool is_active() const __TA_REQUIRES(obj_lock_) {
        return parent_codec() != nullptr;
    }

    // Methods callable from subclasses
    mx_status_t PublishDeviceLocked() __TA_REQUIRES(obj_lock_);

    // Overloads to control stream behavior.
    virtual mx_status_t OnActivateLocked()    __TA_REQUIRES(obj_lock_);
    virtual void        OnDeactivateLocked()  __TA_REQUIRES(obj_lock_);
    virtual void        OnChannelDeactivateLocked(const DispatcherChannel& channel)
        __TA_REQUIRES(obj_lock_);
    virtual mx_status_t OnDMAAssignedLocked() __TA_REQUIRES(obj_lock_);
    virtual mx_status_t OnSolicitedResponseLocked(const CodecResponse& resp)
        __TA_REQUIRES(obj_lock_);
    virtual mx_status_t OnUnsolicitedResponseLocked(const CodecResponse& resp)
        __TA_REQUIRES(obj_lock_);
    virtual mx_status_t BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt)
        __TA_REQUIRES(obj_lock_);
    virtual mx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt)
        __TA_REQUIRES(obj_lock_);
    virtual void OnGetGainLocked(audio_proto::GetGainResp* out_resp) __TA_REQUIRES(obj_lock_);
    virtual void OnSetGainLocked(const audio_proto::SetGainReq& req,
                                 audio_proto::SetGainResp* out_resp) __TA_REQUIRES(obj_lock_);
    virtual void OnPlugDetectLocked(DispatcherChannel* response_channel,
                                    const audio_proto::PlugDetectReq& req,
                                    audio_proto::PlugDetectResp* out_resp)
        __TA_REQUIRES(obj_lock_);

    // Debug logging
    virtual void PrintDebugPrefix() const;

    mx_status_t SendCodecCommandLocked(uint16_t nid, CodecVerb verb, Ack do_ack)
        __TA_REQUIRES(obj_lock_);

    mx_status_t SendCodecCommand(uint16_t nid, CodecVerb verb, Ack do_ack)
        __TA_EXCLUDES(obj_lock_) {
        mxtl::AutoLock obj_lock(&obj_lock_);
        return SendCodecCommandLocked(nid, verb, do_ack);
    }

    // Exposed to derived class for thread annotations.
    const mxtl::Mutex& obj_lock() const __TA_RETURN_CAPABILITY(obj_lock_) { return obj_lock_; }

    // DispatcherChannel::Owner implementation
    mx_status_t ProcessChannel(DispatcherChannel* channel) final;
    void NotifyChannelDeactivated(const DispatcherChannel& channel) final;

    // Unsolicited tag allocation for streams.
    mx_status_t AllocateUnsolTagLocked(uint8_t* out_tag) __TA_REQUIRES(obj_lock_);
    void ReleaseUnsolTagLocked(uint8_t tag) __TA_REQUIRES(obj_lock_);

private:
    mx_status_t SetDMAStreamLocked(uint16_t id, uint8_t tag) __TA_REQUIRES(obj_lock_);
    mx_status_t DoSetStreamFormatLocked(DispatcherChannel* channel,
                                        const audio_proto::StreamSetFmtReq& fmt)
        __TA_REQUIRES(obj_lock_);
    mx_status_t DoGetGainLocked(DispatcherChannel* channel, const audio_proto::GetGainReq& req)
        __TA_REQUIRES(obj_lock_);
    mx_status_t DoSetGainLocked(DispatcherChannel* channel, const audio_proto::SetGainReq& req)
        __TA_REQUIRES(obj_lock_);
    mx_status_t DoPlugDetectLocked(DispatcherChannel* channel,
                                   const audio_proto::PlugDetectReq& req) __TA_REQUIRES(obj_lock_);

    mx_status_t DeviceIoctl(uint32_t op,
                            const void* in_buf,
                            size_t in_len,
                            void* out_buf,
                            size_t out_len,
                            size_t* out_actual) __TA_EXCLUDES(obj_lock_);

    const uint32_t id_;
    const bool     is_input_;
    char           dev_name_[MX_DEVICE_NAME_MAX] = { 0 };

    mxtl::Mutex obj_lock_;

    mxtl::RefPtr<IntelHDACodecDriverBase> parent_codec_  __TA_GUARDED(obj_lock_);
    mxtl::RefPtr<DispatcherChannel>       codec_channel_ __TA_GUARDED(obj_lock_);

    uint16_t dma_stream_id_  __TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_ID;
    uint8_t  dma_stream_tag_ __TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_TAG;

    mx_device_t* parent_device_ __TA_GUARDED(obj_lock_) = nullptr;
    mx_device_t* stream_device_ __TA_GUARDED(obj_lock_) = nullptr;

    mxtl::RefPtr<DispatcherChannel> stream_channel_ __TA_GUARDED(obj_lock_);

    uint32_t set_format_tid_  __TA_GUARDED(obj_lock_) = AUDIO_INVALID_TRANSACTION_ID;
    uint16_t encoded_fmt_     __TA_GUARDED(obj_lock_);
    uint32_t unsol_tag_count_ __TA_GUARDED(obj_lock_) = 0;

    static mx_status_t EncodeStreamFormat(const audio_proto::StreamSetFmtReq& fmt,
                                          uint16_t* encoded_fmt_out);

    static mx_protocol_device_t STREAM_DEVICE_THUNKS;
};

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
