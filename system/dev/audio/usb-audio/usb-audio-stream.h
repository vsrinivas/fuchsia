// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/device-internal.h>
#include <driver/usb.h>
#include <zircon/listnode.h>
#include <lib/zx/vmo.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include <audio-proto/audio-proto.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>

namespace audio {
namespace usb {

struct AudioStreamProtocol : public ddk::internal::base_protocol {
    explicit AudioStreamProtocol(bool is_input) {
        ddk_proto_id_ = is_input ? ZX_PROTOCOL_AUDIO_INPUT : ZX_PROTOCOL_AUDIO_OUTPUT;
    }

    bool is_input() const { return (ddk_proto_id_ == ZX_PROTOCOL_AUDIO_INPUT); }
};

class UsbAudioStream;
using UsbAudioStreamBase = ddk::Device<UsbAudioStream,
                                       ddk::Ioctlable,
                                       ddk::Unbindable>;

class UsbAudioStream : public UsbAudioStreamBase,
                       public AudioStreamProtocol,
                       public fbl::RefCounted<UsbAudioStream> {
public:
    static zx_status_t Create(bool is_input,
                              zx_device_t* parent,
                              usb_protocol_t* usb,
                              int index,
                              usb_interface_descriptor_t* usb_interface,
                              usb_endpoint_descriptor_t* usb_endpoint,
                              usb_audio_ac_format_type_i_desc* format_desc);

    void PrintDebugPrefix() const;

    // DDK device implementation
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op,
                         const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

private:
    friend class fbl::RefPtr<UsbAudioStream>;

    enum class RingBufferState {
        STOPPED,
        STOPPING,
        STOPPING_AFTER_UNPLUG,
        STARTING,
        STARTED,
    };

    UsbAudioStream(zx_device_t* parent,
                   usb_protocol_t* usb,
                   bool is_input,
                   int usb_index,
                   fbl::RefPtr<dispatcher::ExecutionDomain>&& default_domain)
        : UsbAudioStreamBase(parent),
          AudioStreamProtocol(is_input),
          usb_(*usb),
          default_domain_(fbl::move(default_domain)),
          usb_index_(usb_index),
          create_time_(zx_clock_get(ZX_CLOCK_MONOTONIC)) { }

    virtual ~UsbAudioStream();

    zx_status_t Bind(const char* devname,
                     usb_interface_descriptor_t* usb_interface,
                     usb_endpoint_descriptor_t* usb_endpoint,
                     usb_audio_ac_format_type_i_desc* format_desc);

    void ReleaseRingBufferLocked() __TA_REQUIRES(lock_);

    zx_status_t AddFormats(const usb_audio_ac_format_type_i_desc& format_desc,
                           fbl::Vector<audio_stream_format_range_t>* supported_formats);

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
    zx_status_t ProcessRingBufferChannel(dispatcher::Channel* channel);
    void DeactivateRingBufferChannel(const dispatcher::Channel* channel);

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

    void RequestComplete(usb_request_t* req);
    void QueueRequestLocked() __TA_REQUIRES(req_lock_);
    void CompleteRequestLocked(usb_request_t* req) __TA_REQUIRES(req_lock_);

    usb_protocol_t usb_;
    fbl::Mutex lock_;
    fbl::Mutex req_lock_ __TA_ACQUIRED_AFTER(lock_);

    // Dispatcher framework state
    fbl::RefPtr<dispatcher::Channel> stream_channel_ __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::Channel> rb_channel_     __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

    // TODO(johngro) : support parsing and selecting from all of the format
    // descriptors present for a stream, not just a single format (with multiple
    // sample rates).
    fbl::Vector<audio_stream_format_range_t> supported_formats_;
    bool fixed_sample_rate_ = false;

    uint32_t frame_size_;
    uint32_t iso_packet_rate_;
    uint32_t bytes_per_packet_;
    uint32_t fifo_bytes_;
    uint32_t fractional_bpp_inc_;
    uint32_t fractional_bpp_acc_ __TA_GUARDED(req_lock_);
    uint32_t ring_buffer_offset_ __TA_GUARDED(req_lock_);
    uint64_t usb_frame_num_ __TA_GUARDED(req_lock_);

    uint32_t bytes_per_notification_ = 0;
    uint32_t notification_acc_ __TA_GUARDED(req_lock_);

    zx::vmo  ring_buffer_vmo_;
    void*    ring_buffer_virt_  = nullptr;
    uint32_t ring_buffer_size_  = 0;
    uint32_t ring_buffer_pos_ __TA_GUARDED(req_lock_);
    volatile RingBufferState ring_buffer_state_
        __TA_GUARDED(req_lock_) = RingBufferState::STOPPED;

    union {
        audio_proto::RingBufStopResp  stop;
        audio_proto::RingBufStartResp start;
    } pending_job_resp_ __TA_GUARDED(req_lock_);

    list_node_t     free_req_ __TA_GUARDED(req_lock_);
    uint32_t        free_req_cnt_ __TA_GUARDED(req_lock_);
    uint32_t        allocated_req_cnt_;
    uint32_t        max_req_size_;

    uint8_t         iface_num_   = 0;
    uint8_t         alt_setting_ = 0;
    uint8_t         usb_ep_addr_ = 0;

    const int       usb_index_;
    const zx_time_t create_time_;

    // TODO(johngro) : See MG-940.  eliminate this ASAP
    bool req_complete_prio_bumped_ = false;
};

}  // namespace usb
}  // namespace audio
