// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <ddktl/device-internal.h>
#include <zircon/listnode.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include <audio-proto/audio-proto.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-timer.h>
#include <soc/aml-a113/aml-tdm.h>

namespace audio {
namespace gauss {

struct TdmOutputStreamProtocol : public ddk::internal::base_protocol {
    explicit TdmOutputStreamProtocol() {
        ddk_proto_id_ = ZX_PROTOCOL_AUDIO_OUTPUT;
    }
};

class TdmOutputStream;
using TdmAudioStreamBase = ddk::Device<TdmOutputStream,
                                       ddk::Ioctlable,
                                       ddk::Unbindable>;

class TdmOutputStream : public TdmAudioStreamBase,
                       public TdmOutputStreamProtocol,
                       public fbl::RefCounted<TdmOutputStream> {
public:
    static zx_status_t Create(zx_device_t* parent);

    //void PrintDebugPrefix() const;

    // DDK device implementation
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op,
                         const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

private:
    static int IrqThread(void* arg);

    friend class fbl::RefPtr<TdmOutputStream>;

    // TODO(hollande) - the fifo bytes are adjustable on the audio fifos and should be scaled
    //                  with the desired sample rate.  Since this first pass has a fixed sample
    //                  sample rate we will set as constant for now.
    //                  We are using fifo C at this stage, which is max of 128 (64-bit wide)
    //                  Using 64 levels for now.
    static constexpr uint8_t kFifoDepth = 0x40;

    TdmOutputStream(zx_device_t* parent,
                   fbl::RefPtr<dispatcher::ExecutionDomain>&& default_domain)
        : TdmAudioStreamBase(parent),
          TdmOutputStreamProtocol(),
          default_domain_(fbl::move(default_domain)),
          create_time_(zx_clock_get(ZX_CLOCK_MONOTONIC)) { }

    virtual ~TdmOutputStream();

    zx_status_t Bind(const char* devname);

    void ReleaseRingBufferLocked() __TA_REQUIRES(lock_);

    zx_status_t AddFormats(fbl::Vector<audio_stream_format_range_t>* supported_formats);

    // Thunks for dispatching stream channel events.
    zx_status_t ProcessStreamChannel(dispatcher::Channel* channel, bool privileged);
    void DeactivateStreamChannel(const dispatcher::Channel* channel);

    zx_status_t OnGetStreamFormatsLocked(dispatcher::Channel* channel,
                                         const audio_proto::StreamGetFmtsReq& req)
        __TA_REQUIRES(lock_);
    zx_status_t OnSetStreamFormatLocked(dispatcher::Channel* channel,
                                        const audio_proto::StreamSetFmtReq& req,
                                        bool privileged)
        __TA_REQUIRES(lock_);
    zx_status_t OnGetGainLocked(dispatcher::Channel* channel, const audio_proto::GetGainReq& req)
        __TA_REQUIRES(lock_);
    zx_status_t OnSetGainLocked(dispatcher::Channel* channel, const audio_proto::SetGainReq& req)
        __TA_REQUIRES(lock_);
    zx_status_t OnPlugDetectLocked(dispatcher::Channel* channel,
                                   const audio_proto::PlugDetectReq& req) __TA_REQUIRES(lock_);

    // Thunks for dispatching ring buffer channel events.
    zx_status_t ProcessRingBufferChannel(dispatcher::Channel * channel);

    void DeactivateRingBufferChannel(const dispatcher::Channel* channel);

    zx_status_t SetModuleClocks();

    zx_status_t ProcessRingNotification();

    // Stream command handlers
    // Ring buffer command handlers
    zx_status_t OnGetFifoDepthLocked(dispatcher::Channel* channel,
            const audio_proto::RingBufGetFifoDepthReq& req) __TA_REQUIRES(lock_);
    zx_status_t OnGetBufferLocked(dispatcher::Channel* channel,
            const audio_proto::RingBufGetBufferReq& req) __TA_REQUIRES(lock_);
    zx_status_t OnStartLocked(dispatcher::Channel* channel, const audio_proto::RingBufStartReq& req)
        __TA_REQUIRES(lock_);
    zx_status_t OnStopLocked(dispatcher::Channel* channel, const audio_proto::RingBufStopReq& req)
        __TA_REQUIRES(lock_);

    fbl::Mutex lock_;
    fbl::Mutex req_lock_ __TA_ACQUIRED_AFTER(lock_);

    // Dispatcher framework state
    fbl::RefPtr<dispatcher::Channel> stream_channel_ __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::Channel> rb_channel_     __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;


    // control registers for the tdm block
    aml_tdm_regs_t* regs_ = nullptr;
    zx::vmo regs_vmo_;

    fbl::RefPtr<dispatcher::Timer> notify_timer_;

    // TODO(johngro) : support parsing and selecting from all of the format
    // descriptors present for a stream, not just a single format (with multiple
    // sample rates).
    fbl::Vector<audio_stream_format_range_t> supported_formats_;

    platform_device_protocol_t pdev_;
    i2c_protocol_t i2c_;

    fbl::unique_ptr<Tas57xx> left_sub_;
    fbl::unique_ptr<Tas57xx> right_sub_;
    fbl::unique_ptr<Tas57xx> tweeters_;

    float     current_gain_= -20.0;

    uint32_t frame_size_;
    uint32_t fifo_bytes_;

    const zx_time_t create_time_;
    uint32_t us_per_notification_ = 0;
    volatile bool running_;

    zx::bti bti_;
    io_buffer_t ring_buffer_;
    void*    ring_buffer_virt_  = nullptr;
    uint32_t ring_buffer_phys_  = 0;
    uint32_t ring_buffer_size_  = 0;
};

}  // namespace usb
}  // namespace audio
