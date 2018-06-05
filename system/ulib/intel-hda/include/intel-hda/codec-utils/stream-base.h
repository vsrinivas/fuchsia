// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <lib/zx/channel.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include <audio-proto/audio-proto.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/intel-hda-proto.h>

namespace audio {
namespace intel_hda {
namespace codecs {

class IntelHDACodecDriverBase;

class IntelHDAStreamBase : public fbl::RefCounted<IntelHDAStreamBase>,
                           public fbl::WAVLTreeContainable<fbl::RefPtr<IntelHDAStreamBase>> {
public:
    zx_status_t Activate(fbl::RefPtr<IntelHDACodecDriverBase>&& parent_codec,
                         const fbl::RefPtr<dispatcher::Channel>& codec_channel)
        __TA_EXCLUDES(obj_lock_);

    void Deactivate() __TA_EXCLUDES(obj_lock_, default_domain_->token());

    zx_status_t ProcessResponse(const CodecResponse& resp) __TA_EXCLUDES(obj_lock_);
    zx_status_t ProcessRequestStream(const ihda_proto::RequestStreamResp& resp)
        __TA_EXCLUDES(obj_lock_);
    virtual zx_status_t ProcessSetStreamFmt(const ihda_proto::SetStreamFmtResp& resp,
                                            zx::channel&& ring_buffer_channel)
        __TA_EXCLUDES(obj_lock_);

    uint32_t id()       const { return id_; }
    bool     is_input() const { return is_input_; }
    uint32_t GetKey()   const { return id(); }

protected:
    friend class fbl::RefPtr<IntelHDAStreamBase>;

    enum class Ack {
        NO,
        YES,
    };

    IntelHDAStreamBase(uint32_t id, bool is_input);
    virtual ~IntelHDAStreamBase();

    void SetPersistentUniqueId(const audio_stream_unique_id_t& id)
        __TA_EXCLUDES(obj_lock_);

    // Properties available to subclasses.
    uint8_t dma_stream_tag() const __TA_REQUIRES(obj_lock_) {
        return dma_stream_tag_;
    }

    const fbl::RefPtr<IntelHDACodecDriverBase>& parent_codec() const __TA_REQUIRES(obj_lock_) {
        return parent_codec_;
    }

    bool is_active() const __TA_REQUIRES(obj_lock_) {
        return parent_codec() != nullptr;
    }

    fbl::Mutex* obj_lock() __TA_RETURN_CAPABILITY(obj_lock_) { return &obj_lock_; }

    fbl::RefPtr<dispatcher::ExecutionDomain> domain() const { return default_domain_; }
    fbl::RefPtr<dispatcher::Channel> stream_channel() const __TA_REQUIRES(obj_lock_) {
        return stream_channel_;
    }
    uint32_t set_format_tid() const __TA_REQUIRES(obj_lock_) { return set_format_tid_; }
    uint16_t encoded_fmt()    const __TA_REQUIRES(obj_lock_) { return encoded_fmt_; }

    // Methods callable from subclasses
    zx_status_t PublishDeviceLocked() __TA_REQUIRES(obj_lock_);
    void SetSupportedFormatsLocked(fbl::Vector<audio_proto::FormatRange>&& formats)
        __TA_REQUIRES(obj_lock_) {
        supported_formats_ = fbl::move(formats);
    }
    void SetFormatTidLocked(uint32_t set_format_tid) __TA_REQUIRES(obj_lock_) {
        set_format_tid_ = set_format_tid;
    }

    // Overloads to control stream behavior.
    virtual zx_status_t OnActivateLocked()    __TA_REQUIRES(obj_lock_);
    virtual void        OnDeactivateLocked()  __TA_REQUIRES(obj_lock_);
    virtual void        OnChannelDeactivateLocked(const dispatcher::Channel& channel)
        __TA_REQUIRES(obj_lock_);
    virtual zx_status_t OnDMAAssignedLocked() __TA_REQUIRES(obj_lock_);
    virtual zx_status_t OnSolicitedResponseLocked(const CodecResponse& resp)
        __TA_REQUIRES(obj_lock_);
    virtual zx_status_t OnUnsolicitedResponseLocked(const CodecResponse& resp)
        __TA_REQUIRES(obj_lock_);
    virtual zx_status_t BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt)
        __TA_REQUIRES(obj_lock_);
    virtual zx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt)
        __TA_REQUIRES(obj_lock_);
    virtual void OnGetGainLocked(audio_proto::GetGainResp* out_resp) __TA_REQUIRES(obj_lock_);
    virtual void OnSetGainLocked(const audio_proto::SetGainReq& req,
                                 audio_proto::SetGainResp* out_resp) __TA_REQUIRES(obj_lock_);
    virtual void OnPlugDetectLocked(dispatcher::Channel* response_channel,
                                    const audio_proto::PlugDetectReq& req,
                                    audio_proto::PlugDetectResp* out_resp)
        __TA_REQUIRES(obj_lock_);
    virtual void  OnGetStringLocked(const audio_proto::GetStringReq& req,
                                    audio_proto::GetStringResp* out_resp)
        __TA_REQUIRES(obj_lock_);

    // Debug logging
    virtual void PrintDebugPrefix() const;

    zx_status_t SendCodecCommandLocked(uint16_t nid, CodecVerb verb, Ack do_ack)
        __TA_REQUIRES(obj_lock_);

    zx_status_t SendCodecCommand(uint16_t nid, CodecVerb verb, Ack do_ack)
        __TA_EXCLUDES(obj_lock_) {
        fbl::AutoLock obj_lock(&obj_lock_);
        return SendCodecCommandLocked(nid, verb, do_ack);
    }

    // Exposed to derived class for thread annotations.
    const fbl::Mutex& obj_lock() const __TA_RETURN_CAPABILITY(obj_lock_) { return obj_lock_; }

    // Unsolicited tag allocation for streams.
    zx_status_t AllocateUnsolTagLocked(uint8_t* out_tag) __TA_REQUIRES(obj_lock_);
    void ReleaseUnsolTagLocked(uint8_t tag) __TA_REQUIRES(obj_lock_);

private:
    // Thunks for dispatching channel events.
    zx_status_t ProcessClientRequest(dispatcher::Channel* channel, bool privileged);
    void ProcessClientDeactivate(const dispatcher::Channel* channel, bool privileged);

    zx_status_t DoGetStreamFormatsLocked(dispatcher::Channel* channel,
                                         bool privileged,
                                         const audio_proto::StreamGetFmtsReq& req)
        __TA_REQUIRES(obj_lock_);
    zx_status_t DoSetStreamFormatLocked(dispatcher::Channel* channel,
                                        bool privileged,
                                        const audio_proto::StreamSetFmtReq& fmt)
        __TA_REQUIRES(obj_lock_);
    zx_status_t DoGetGainLocked(dispatcher::Channel* channel,
                                bool privileged,
                                const audio_proto::GetGainReq& req)
        __TA_REQUIRES(obj_lock_);
    zx_status_t DoSetGainLocked(dispatcher::Channel* channel,
                                bool privileged,
                                const audio_proto::SetGainReq& req)
        __TA_REQUIRES(obj_lock_);
    zx_status_t DoPlugDetectLocked(dispatcher::Channel* channel,
                                   bool privileged,
                                   const audio_proto::PlugDetectReq& req)
        __TA_REQUIRES(obj_lock_);
    zx_status_t DoGetUniqueIdLocked(dispatcher::Channel* channel,
                                    bool privileged,
                                    const audio_proto::GetUniqueIdReq& req)
        __TA_REQUIRES(obj_lock_);
    zx_status_t DoGetStringLocked(dispatcher::Channel* channel,
                                  bool privileged,
                                  const audio_proto::GetStringReq& req)
        __TA_REQUIRES(obj_lock_);

    zx_status_t SetDMAStreamLocked(uint16_t id, uint8_t tag) __TA_REQUIRES(obj_lock_);
    zx_status_t DeviceIoctl(uint32_t op,
                            const void* in_buf,
                            size_t in_len,
                            void* out_buf,
                            size_t out_len,
                            size_t* out_actual) __TA_EXCLUDES(obj_lock_);

    const uint32_t id_;
    const bool     is_input_;
    char           dev_name_[ZX_DEVICE_NAME_MAX] = { 0 };

    fbl::Mutex obj_lock_;

    fbl::RefPtr<IntelHDACodecDriverBase>     parent_codec_  __TA_GUARDED(obj_lock_);
    fbl::RefPtr<dispatcher::Channel>         codec_channel_ __TA_GUARDED(obj_lock_);
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

    uint16_t dma_stream_id_  __TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_ID;
    uint8_t  dma_stream_tag_ __TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_TAG;

    zx_device_t* parent_device_ __TA_GUARDED(obj_lock_) = nullptr;
    zx_device_t* stream_device_ __TA_GUARDED(obj_lock_) = nullptr;

    fbl::RefPtr<dispatcher::Channel>       stream_channel_    __TA_GUARDED(obj_lock_);
    fbl::Vector<audio_proto::FormatRange>  supported_formats_ __TA_GUARDED(obj_lock_);

    uint32_t set_format_tid_  __TA_GUARDED(obj_lock_) = AUDIO_INVALID_TRANSACTION_ID;
    uint16_t encoded_fmt_     __TA_GUARDED(obj_lock_);
    uint32_t unsol_tag_count_ __TA_GUARDED(obj_lock_) = 0;
    audio_stream_unique_id_t persistent_unique_id_;

    static zx_status_t EncodeStreamFormat(const audio_proto::StreamSetFmtReq& fmt,
                                          uint16_t* encoded_fmt_out);

    static zx_protocol_device_t STREAM_DEVICE_THUNKS;
};

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
