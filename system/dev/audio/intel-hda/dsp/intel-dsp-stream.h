// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

#include <intel-hda/codec-utils/stream-base.h>

#include "intel-dsp-topology.h"
#include "debug-logging.h"

namespace audio {
namespace intel_hda {

class IntelAudioDsp;

class IntelDspStream : public codecs::IntelHDAStreamBase {
public:
    IntelDspStream(uint32_t id, bool is_input, const DspPipeline& pipeline,
                   const audio_stream_unique_id_t* unique_id = nullptr);

    // Overloaded
    zx_status_t ProcessSetStreamFmt(const ihda_proto::SetStreamFmtResp& resp,
                                    zx::channel&& ring_buffer_channel)
        __TA_EXCLUDES(obj_lock()) override;

    const char* log_prefix() const { return log_prefix_; }

protected:
    virtual ~IntelDspStream() { }

    zx_status_t OnActivateLocked()    __TA_REQUIRES(obj_lock()) final;
    void        OnDeactivateLocked()  __TA_REQUIRES(obj_lock()) final;
    void        OnChannelDeactivateLocked(const dispatcher::Channel& channel)
        __TA_REQUIRES(obj_lock()) final;
    zx_status_t OnDMAAssignedLocked() __TA_REQUIRES(obj_lock()) final;
    zx_status_t OnSolicitedResponseLocked(const CodecResponse& resp)
        __TA_REQUIRES(obj_lock()) final;
    zx_status_t OnUnsolicitedResponseLocked(const CodecResponse& resp)
        __TA_REQUIRES(obj_lock()) final;
    zx_status_t BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt)
        __TA_REQUIRES(obj_lock()) final;
    zx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt)
        __TA_REQUIRES(obj_lock()) final;
    void OnGetGainLocked(audio_proto::GetGainResp* out_resp)
        __TA_REQUIRES(obj_lock()) final;
    void OnSetGainLocked(const audio_proto::SetGainReq& req,
                         audio_proto::SetGainResp* out_resp) __TA_REQUIRES(obj_lock()) final;
    void OnPlugDetectLocked(dispatcher::Channel* response_channel,
                            const audio_proto::PlugDetectReq& req,
                            audio_proto::PlugDetectResp* out_resp) __TA_REQUIRES(obj_lock()) final;
    void OnGetStringLocked(const audio_proto::GetStringReq& req,
                           audio_proto::GetStringResp* out_resp) __TA_REQUIRES(obj_lock()) final;

private:
    friend class fbl::RefPtr<IntelDspStream>;

    zx_status_t CreateClientRingBufferChannelLocked(zx::channel&& ring_buffer_channel,
                                                    zx::channel* out_client_channel)
        __TA_REQUIRES(obj_lock());

    zx_status_t ProcessRbRequest(dispatcher::Channel* channel);
    void ProcessRbDeactivate(const dispatcher::Channel* channel);

    zx_status_t ProcessClientRbRequest(dispatcher::Channel* channel);
    void ProcessClientRbDeactivate(const dispatcher::Channel* channel);

    // Log prefix storage
    char log_prefix_[LOG_PREFIX_STORAGE] = { 0 };

    const DspPipeline pipeline_;

    fbl::RefPtr<dispatcher::Channel> rb_channel_ __TA_GUARDED(obj_lock());
    fbl::RefPtr<dispatcher::Channel> client_rb_channel_ __TA_GUARDED(obj_lock());
};

}  // namespace intel_hda
}  // namespace audio
