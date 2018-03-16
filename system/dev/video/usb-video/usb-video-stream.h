// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <driver/usb.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/vector.h>
#include <zircon/device/camera-proto.h>
#include <lib/zx/vmo.h>

#include "usb-video.h"
#include "video-buffer.h"

namespace video {
namespace usb {

static constexpr int USB_ENDPOINT_INVALID = -1;

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

    // Maps between a camera video format proto and pointers
    // to the corresponding driver format and frame descriptors.
    struct FormatMapping {
        FormatMapping(const UsbVideoFormat* format,
                      const UsbVideoFrameDesc* frame_desc);
        camera::camera_proto::VideoFormat proto;

        const UsbVideoFormat* format;
        const UsbVideoFrameDesc* frame_desc;
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
    // If successful, sets the initial state of the stream configuration
    // related fields, and reallocates usb requests if necessary.
    // Otherwise an error will be returned and the caller should try
    // again with a different set of inputs.
    //
    // frame_desc may be NULL for non frame based formats.
    zx_status_t TryFormatLocked(const UsbVideoFormat* format,
                                const UsbVideoFrameDesc* frame_desc)
        __TA_REQUIRES(lock_);

    zx_status_t ProcessStreamChannel(dispatcher::Channel* channel);
    zx_status_t ProcessVideoBufferChannel(dispatcher::Channel* channel);

    // Finds the matching format and frame descriptors for the given
    // video format proto.
    // If found returns ZX_OK and populates out_format and out_frame_desc,
    // else returns an error.
    zx_status_t GetMapping(camera::camera_proto::VideoFormat format,
                           const UsbVideoFormat** out_format,
                           const UsbVideoFrameDesc** out_frame_desc);

    // Creates mappings between video format protos and their original
    // format and frame descriptors. The result will be stored in format_mappings_.
    zx_status_t GenerateFormatMappings();

    zx_status_t GetFormatsLocked(dispatcher::Channel* channel,
                                 const camera::camera_proto::GetFormatsReq& req)
        __TA_REQUIRES(lock_);
    zx_status_t SetFormatLocked(dispatcher::Channel* channel,
                                const camera::camera_proto::SetFormatReq& req)
        __TA_REQUIRES(lock_);

    // Creates a new video buffer and maps it into our address space.
    // The current streaming state must be StreamingState::STOPPED.
    zx_status_t CreateDataVideoBuffer();
    zx_status_t SetBufferLocked(dispatcher::Channel* channel,
                                const camera::camera_proto::VideoBufSetBufferReq& req,
                                zx::handle rxed_handle) __TA_REQUIRES(lock_);

    zx_status_t StartStreamingLocked(dispatcher::Channel* channel,
                                     const camera::camera_proto::VideoBufStartReq& req)
        __TA_REQUIRES(lock_);
    zx_status_t StopStreamingLocked(dispatcher::Channel* channel,
                                    const camera::camera_proto::VideoBufStopReq& req)
        __TA_REQUIRES(lock_);

    zx_status_t FrameReleaseLocked(dispatcher::Channel* channel,
                                   const camera::camera_proto::VideoBufFrameReleaseReq& req)
        __TA_REQUIRES(lock_);

    // Populates the free_reqs_ list with usb requests of the specified size.
    // Returns immediately if the list already contains large enough usb
    // requests, otherwise frees existing requests before allocating new ones.
    // The current streaming state must be StreamingState::STOPPED.
    zx_status_t AllocUsbRequestsLocked(uint64_t size) __TA_REQUIRES(lock_);
    // Queues a usb request against the underlying device.
    void QueueRequestLocked() __TA_REQUIRES(lock_);
    void RequestComplete(usb_request_t* req);

    void ParseHeaderTimestamps(usb_request_t* req);
    // Notifies the client regarding the status of the completed frame.
    // If the frame was completed successfully, it will also be locked until the
    // client sends a FRAME_RELEASE request.
    // Returns an error if the completed frame could not be locked or the
    // notification could not be sent.
    zx_status_t FrameNotifyLocked() __TA_REQUIRES(lock_);
    // Parses the payload header from the start of the usb request response.
    // If the header is parsed successfully, ZX_OK is returned and the length
    // of the header stored in out_header_length.
    // Returns an error if the header is malformed.
    zx_status_t ParsePayloadHeaderLocked(usb_request_t* req,
                                         uint32_t* out_header_length)
        __TA_REQUIRES(lock_);
    // Extracts the payload data from the usb request response,
    // and stores it in the video buffer.
    void ProcessPayloadLocked(usb_request_t* req) __TA_REQUIRES(lock_);

    void DeactivateStreamChannel(const dispatcher::Channel* channel);
    void DeactivateVideoBufferChannel(const dispatcher::Channel* channel);

    usb_protocol_t usb_;

    fbl::Vector<UsbVideoFormat> formats_;
    fbl::Vector<UsbVideoStreamingSetting> streaming_settings_;

    fbl::Vector<FormatMapping> format_mappings_;

    // Stream configuration.
    usb_video_vc_probe_and_commit_controls negotiation_result_;
    const UsbVideoStreamingSetting* cur_streaming_setting_;
    const UsbVideoFormat* cur_format_;
    const UsbVideoFrameDesc* cur_frame_desc_;
    uint32_t max_frame_size_ = 0;
    uint32_t clock_frequency_hz_ = 0;
    // The number of bytes to request in a USB request to a streaming endpoint.
    // This should be equal or less than allocated_req_size_.
    uint64_t send_req_size_ = 0;

    int streaming_ep_type_ = USB_ENDPOINT_INVALID;
    uint8_t iface_num_ = 0;
    uint8_t usb_ep_addr_ = 0;

    // Dispatcher framework state
    fbl::RefPtr<dispatcher::Channel> stream_channel_ __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::Channel> vb_channel_     __TA_GUARDED(lock_);
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

    // Statistics for frame based formats.
    // Number of frames encountered.
    uint32_t num_frames_ = 0;

    struct FrameState {
        // Bytes received so far for the frame.
        uint32_t bytes;

        // FID is a bit that is toggled when a new frame begins,
        // and stays constant for the rest of the frame.
        // Some devices will not set this bit.
        int8_t fid;
        // Whether we've received the last payload for the frame.
        // Some devices will not set this bit.
        bool eof;

        // Whether the frame contains an error.
        bool error;
        // Presentation timestamp for the frame. This is when the device
        // begins raw frame capture.
        uint32_t pts;
        // Source time clock value for when the first video data of a
        // video frame is put on the USB bus.
        uint32_t stc;
        // The USB frame number at the time that STC was sampled.
        // The largest value can have 11 bits set before wrapping around to zero.
        uint16_t device_sof;

        // This is the 64 bit incremental frame number at the time the first
        // payload was received by the USB video driver.
        // The XHCI host handles the SOF value wrapping around, so this is 64 bits.
        uint64_t host_sof;

        // The time at the midpoint of the capture operation, with respect
        // to the monotonic clock.
        zx_time_t capture_time;
    };

    FrameState cur_frame_state_;

    fbl::unique_ptr<VideoBuffer> video_buffer_ __TA_GUARDED(lock_);

    // Whether a video buffer frame offset has been obtained to store the
    // data. False if the video buffer was full.
    bool has_video_buffer_offset_ __TA_GUARDED(lock_);
    // Offset into the video buffer of the current frame we're writing to.
    VideoBuffer::FrameOffset video_buffer_offset_ __TA_GUARDED(lock_);

    volatile StreamingState streaming_state_
        __TA_GUARDED(lock_) = StreamingState::STOPPED;

    list_node_t free_reqs_ __TA_GUARDED(lock_);
    uint32_t num_free_reqs_ __TA_GUARDED(lock_);
    uint32_t num_allocated_reqs_ = 0;
    // Size of underlying VMO backing the USB request.
    uint64_t allocated_req_size_ = 0;

    // Bulk transfer specific fields.
    // Total bytes received so far for the current payload, including headers.
    // A bulk payload may be split across multiple usb requests,
    // whereas for isochronous it is always one payload per usb request.
    uint32_t bulk_payload_bytes_ = 0;

    fbl::Mutex lock_;
};

} // namespace usb
} // namespace video
