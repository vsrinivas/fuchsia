// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <camera-proto/camera-proto.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <driver/usb.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/vector.h>
#include <zx/vmo.h>

#include "usb-video.h"

namespace video {
namespace usb {

struct VideoStreamProtocol : public ddk::internal::base_protocol {
    explicit VideoStreamProtocol() {
        ddk_proto_id_ = ZX_PROTOCOL_CAMERA;
    }
};

class UsbVideoStream;
using UsbVideoStreamBase = ddk::Device<UsbVideoStream,
                                       ddk::Ioctlable,
                                       ddk::Unbindable>;

class UsbVideoStream : public UsbVideoStreamBase,
                       public VideoStreamProtocol {

public:
    static zx_status_t Create(zx_device_t* device,
                              usb_protocol_t* usb,
                              int index,
                              usb_interface_descriptor_t* intf,
                              usb_video_vc_header_desc* control_header,
                              usb_video_vs_input_header_desc* input_header,
                              fbl::Vector<UsbVideoFormat>* formats,
                              fbl::Vector<UsbVideoStreamingSetting>* settings);

    // DDK device implementation
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op,
                         const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);
    ~UsbVideoStream();

private:
    enum class StreamingState {
        STOPPED,
        STOPPING,
        STARTED,
    };

    struct RingBuffer {
        zx_status_t Init(uint32_t size);

        zx::vmo vmo;
        void* virt = nullptr;
        uint32_t size = 0;
        uint32_t offset = 0;
    };

     UsbVideoStream(zx_device_t* parent,
                    usb_protocol_t* usb,
                    fbl::Vector<UsbVideoFormat>* formats,
                    fbl::Vector<UsbVideoStreamingSetting>* settings,
                    fbl::RefPtr<dispatcher::ExecutionDomain>&& default_domain)
         : UsbVideoStreamBase(parent),
           usb_(*usb),
           formats_(fbl::move(*formats)),
           streaming_settings_(fbl::move(*settings)),
           default_domain_(fbl::move(default_domain)) {}

    zx_status_t Bind(const char* devname,
                     usb_interface_descriptor_t* intf,
                     usb_video_vc_header_desc* control_header,
                     usb_video_vs_input_header_desc* input_header);

    // Deferred initialization of the device via a thread.  Once complete, this
    // marks the device as visible.
    static zx_status_t Init(void* device) {
        return reinterpret_cast<UsbVideoStream*>(device)->Init();
    }
    zx_status_t Init();
    zx_status_t SetFormat();

    // Requests the device use the given format and frame descriptor,
    // then finds a streaming setting that supports the required
    // data throughput.
    // If successful, out_result will be populated with the result
    // of the stream negotiation, and out_setting will be populated
    // with a pointer to the selected streaming setting.
    // Otherwise an error will be returned and the caller should try
    // again with a different set of inputs.
    //
    // frame_desc may be NULL for non frame based formats.
    zx_status_t TryFormat(const UsbVideoFormat* format,
                          const UsbVideoFrameDesc* frame_desc,
                          usb_video_vc_probe_and_commit_controls* out_result,
                          const UsbVideoStreamingSetting** out_setting);


    zx_status_t ProcessChannel(dispatcher::Channel* channel);

    zx_status_t GetFormatsLocked(dispatcher::Channel* channel,
                                 const camera::camera_proto::GetFormatsReq& req)
        __TA_REQUIRES(lock_);
    zx_status_t SetFormatLocked(dispatcher::Channel* channel,
                                const camera::camera_proto::SetFormatReq& req)
        __TA_REQUIRES(lock_);

    // Creates a new ring buffer and maps it into our address space.
    // The current streaming state must be StreamingState::STOPPED.
    zx_status_t CreateDataRingBuffer();
    zx_status_t StartStreaming();
    zx_status_t StopStreaming();

    // Queues a usb request against the underlying device.
    void QueueRequestLocked() __TA_REQUIRES(lock_);
    void RequestComplete(usb_request_t* req);

    void ParseHeaderTimestamps(usb_request_t* req);
    // Extracts the payload data from the usb request response,
    // and stores it in the ring buffer.
    void ProcessPayloadLocked(usb_request_t* req) __TA_REQUIRES(lock_);

    void DeactivateStreamChannel(const dispatcher::Channel* channel);

    usb_protocol_t usb_;

    fbl::Vector<UsbVideoFormat> formats_;
    fbl::Vector<UsbVideoStreamingSetting> streaming_settings_;

    usb_video_vc_probe_and_commit_controls negotiation_result_;
    const UsbVideoFormat* cur_format_;
    const UsbVideoFrameDesc* cur_frame_desc_;
    const UsbVideoStreamingSetting* cur_streaming_setting_;

    uint8_t iface_num_ = 0;
    uint8_t usb_ep_addr_ = 0;

    // Dispatcher framework state
    fbl::RefPtr<dispatcher::Channel> stream_channel_ __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

    uint32_t clock_frequency_hz_ = 0;

    // Statistics for frame based formats.
    uint32_t max_frame_size_;
    // FID is a bit that is toggled when a new frame begins.
    // Initialized to -1 so that the first frame will be counted as a new frame.
    uint8_t cur_fid_ = -1;
    // Presentation timestamp for the current frame.
    uint32_t cur_frame_pts_ = 0;
    // Source time clock value for the current frame.
    uint32_t cur_frame_stc_ = 0;
    // Number of frames encountered.
    uint32_t num_frames_ = 0;
    // Bytes received so far for the current frame.
    uint32_t cur_frame_bytes_ = 0;
    // Whether the current frame contains an error.
    bool cur_frame_error_ = false;

    RingBuffer data_ring_buffer_ __TA_GUARDED(lock_);

    volatile StreamingState streaming_state_
        __TA_GUARDED(lock_) = StreamingState::STOPPED;

    list_node_t free_reqs_ __TA_GUARDED(lock_);
    uint32_t num_free_reqs_ __TA_GUARDED(lock_);
    uint32_t num_allocated_reqs_ = 0;

    fbl::Mutex lock_;
};

} // namespace usb
} // namespace video
