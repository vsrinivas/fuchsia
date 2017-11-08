// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include "a113-ddr.h"
#include "a113-pdm.h"
#include "audio-proto/audio-proto.h"
#include "dispatcher-pool/dispatcher-channel.h"
#include "dispatcher-pool/dispatcher-execution-domain.h"

#include "a113-ddr.h"
#include "a113-pdm.h"
#include "vmo_helper.h"

namespace audio {
namespace gauss {

struct PdmInputStreamProtocol : public ddk::internal::base_protocol {
    explicit PdmInputStreamProtocol() {
        ddk_proto_id_ = ZX_PROTOCOL_AUDIO_INPUT;
    }
};

class GaussPdmInputStream;
using GaussPdmInputStreamBase =
    ddk::Device<GaussPdmInputStream, ddk::Ioctlable, ddk::Unbindable>;

class GaussPdmInputStream : public GaussPdmInputStreamBase,
                            public PdmInputStreamProtocol,
                            public fbl::RefCounted<GaussPdmInputStream> {
public:
    static zx_status_t Create(zx_device_t* parent);

    // DDK device implementation
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

private:
    friend class fbl::RefPtr<GaussPdmInputStream>;

    GaussPdmInputStream(
        zx_device_t* parent,
        fbl::RefPtr<dispatcher::ExecutionDomain>&& default_domain)
        : GaussPdmInputStreamBase(parent),
          default_domain_(fbl::move(default_domain)) {}

    virtual ~GaussPdmInputStream();

    uint32_t GetFifoBytes();

    static int IrqThread(void* arg);

    zx_status_t Bind(const char* devname, zx_device_t* parent);

    zx_status_t AddFormats();

    // Thunks for dispatching stream channel events.
    zx_status_t ProcessStreamChannel(dispatcher::Channel* channel,
                                     bool privileged);

    void DeactivateStreamChannel(const dispatcher::Channel* channel);

    zx_status_t
    OnGetStreamFormatsLocked(dispatcher::Channel* channel,
                             const audio_proto::StreamGetFmtsReq& req)
        __TA_REQUIRES(lock_);

    zx_status_t OnSetStreamFormatLocked(dispatcher::Channel* channel,
                                        const audio_proto::StreamSetFmtReq& req,
                                        bool privileged) __TA_REQUIRES(lock_);

    zx_status_t OnGetGainLocked(dispatcher::Channel* channel,
                                const audio_proto::GetGainReq& req)
        __TA_REQUIRES(lock_);

    zx_status_t OnSetGainLocked(dispatcher::Channel* channel,
                                const audio_proto::SetGainReq& req)
        __TA_REQUIRES(lock_);

    zx_status_t OnPlugDetectLocked(dispatcher::Channel* channel,
                                   const audio_proto::PlugDetectReq& req)
        __TA_REQUIRES(lock_);

    // Thunks for dispatching ring buffer channel events.
    zx_status_t ProcessRingBufferChannel(dispatcher::Channel* channel);

    void DeactivateRingBufferChannel(const dispatcher::Channel* channel);

    // Stream command handlers
    // Ring buffer command handlers
    zx_status_t
    OnGetFifoDepthLocked(dispatcher::Channel* channel,
                         const audio_proto::RingBufGetFifoDepthReq& req)
        __TA_REQUIRES(lock_);

    zx_status_t OnGetBufferLocked(dispatcher::Channel* channel,
                                  const audio_proto::RingBufGetBufferReq& req)
        __TA_REQUIRES(lock_);

    zx_status_t OnStartLocked(dispatcher::Channel* channel,
                              const audio_proto::RingBufStartReq& req)
        __TA_REQUIRES(lock_);

    zx_status_t OnStopLocked(dispatcher::Channel* channel,
                             const audio_proto::RingBufStopReq& req)
        __TA_REQUIRES(lock_);

    void RequestComplete();

    fbl::Mutex lock_;

    // Dispatcher framework state
    fbl::RefPtr<dispatcher::Channel> stream_channel_ __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::Channel> rb_channel_ __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

    fbl::Vector<audio_stream_format_range_t> supported_formats_;

    uint32_t frame_size_;

    VmoHelper<false> vmo_helper_;

    // TODO(almasrymina): hardcoded.
    uint32_t frame_rate_ = 48000;

    a113_audio_device_t audio_device_;
    thrd_t irqthrd_;
};

} // namespace gauss
} // namespace audio
