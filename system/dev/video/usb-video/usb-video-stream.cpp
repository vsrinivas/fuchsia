// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <stdlib.h>
#include <string.h>
#include <zx/vmar.h>

#include "usb-video-stream.h"
#include "video-util.h"

namespace video {
namespace usb {

// TODO(jocelyndang): calculate this rather than hardcoding.
static constexpr uint32_t RING_BUFFER_NUM_FRAMES = 30;
static constexpr uint32_t MAX_OUTSTANDING_REQS = 8;

UsbVideoStream::~UsbVideoStream() {
    // List may not have been initialized.
    if (free_reqs_.next) {
        while (!list_is_empty(&free_reqs_)) {
            usb_request_release(list_remove_head_type(&free_reqs_, usb_request_t, node));
        }
    }
    if (data_ring_buffer_.virt != nullptr) {
        zx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(data_ring_buffer_.virt),
                                    data_ring_buffer_.size);
    }
}

// static
zx_status_t UsbVideoStream::Create(zx_device_t* device,
                                   usb_protocol_t* usb,
                                   int index,
                                   usb_interface_descriptor_t* intf,
                                   usb_video_vc_header_desc* control_header,
                                   usb_video_vs_input_header_desc* input_header,
                                   fbl::Vector<UsbVideoFormat>* formats,
                                   fbl::Vector<UsbVideoStreamingSetting>* settings) {
    if (!usb || !intf || !control_header || !input_header ||
        !formats || formats->size() == 0 || !settings) {
        return ZX_ERR_INVALID_ARGS;
    }
    auto domain = dispatcher::ExecutionDomain::Create();
    if (domain == nullptr) { return ZX_ERR_NO_MEMORY; }

    auto dev = fbl::unique_ptr<UsbVideoStream>(
        new UsbVideoStream(device, usb, formats, settings, fbl::move(domain)));

    char name[ZX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "usb-video-source-%d", index);

    auto status = dev->Bind(name, intf, control_header, input_header);
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

zx_status_t UsbVideoStream::Bind(const char* devname,
                                 usb_interface_descriptor_t* intf,
                                 usb_video_vc_header_desc* control_header,
                                 usb_video_vs_input_header_desc* input_header) {
    iface_num_ = intf->bInterfaceNumber;
    clock_frequency_hz_ = control_header->dwClockFrequency;
    usb_ep_addr_ = input_header->bEndpointAddress;

    uint32_t max_bandwidth = 0;
    for (const auto& setting : streaming_settings_) {
        uint32_t bandwidth = setting_bandwidth(setting);
        if (bandwidth > max_bandwidth) {
            max_bandwidth = bandwidth;
        }
    }

    zxlogf(TRACE, "allocating %d usb requests of size %u\n",
           MAX_OUTSTANDING_REQS, max_bandwidth);

    {
        fbl::AutoLock lock(&lock_);

        list_initialize(&free_reqs_);

        for (uint32_t i = 0; i < MAX_OUTSTANDING_REQS; i++) {
            usb_request_t* req;
            zx_status_t status = usb_request_alloc(&req,
                                                   max_bandwidth,
                                                   usb_ep_addr_);
            if (status != ZX_OK) {
                zxlogf(ERROR, "usb_request_alloc failed: %d\n", status);
                return status;
            }

            req->cookie = this;
            req->complete_cb = [](usb_request_t* req, void* cookie) -> void {
                ZX_DEBUG_ASSERT(cookie != nullptr);
                reinterpret_cast<UsbVideoStream*>(cookie)->RequestComplete(req);
            };
            list_add_head(&free_reqs_, &req->node);
            num_free_reqs_++;
            num_allocated_reqs_++;
        }
    }

    zx_status_t status = UsbVideoStreamBase::DdkAdd(devname, DEVICE_ADD_INVISIBLE);
    if (status != ZX_OK) {
        return status;
    }

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, Init, this, "usb_video:init");
    if (ret != thrd_success) {
        DdkRemove();
        return ZX_ERR_INTERNAL;
    }
    thrd_detach(thread);
    return ZX_OK;
}

zx_status_t UsbVideoStream::Init() {
    zx_status_t status = SetFormat();
    if (status != ZX_OK) {
        DdkRemove();
        return status;
    }
    DdkMakeVisible();
    return ZX_OK;
}

zx_status_t UsbVideoStream::SetFormat() {
    fbl::AutoLock lock(&lock_);

    if (streaming_state_ != StreamingState::STOPPED) {
        // TODO(jocelyndang): stop the ring buffer rather than returning an error.
        return ZX_ERR_BAD_STATE;
    }

    // TODO(jocelyndang): add a way for the client to select the format and
    // frame type. Just use the first format for now.
    UsbVideoFormat* format = formats_.get();
    if (!format) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    const UsbVideoFrameDesc* try_frame = NULL;
    // Try the recommended frame descriptor, if any.
    if (format->default_frame_index != 0) {
        for (const auto& frame : format->frame_descs) {
            if (frame.index == format->default_frame_index) {
                try_frame = &frame;
            }
        }
        if (!try_frame) {
            return ZX_ERR_INTERNAL;
        }
    }
    zx_status_t status = TryFormat(format, try_frame, &negotiation_result_,
                                   &cur_streaming_setting_);
    if (status != ZX_OK) {
        // Negotiation failed. Try a different frame descriptor.
        for (const auto& frame : format->frame_descs) {
            if (frame.index == format->default_frame_index) {
                // Already tried this setting.
                continue;
            }
            try_frame = &frame;
            status = TryFormat(format, try_frame, &negotiation_result_,
                               &cur_streaming_setting_);
            if (status == ZX_OK) {
                break;
            }
        }
    }
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to set format %u: error %d\n", format->index, status);
        return status;
    }

    max_frame_size_ = negotiation_result_.dwMaxVideoFrameSize;
    cur_format_ = format;
    cur_frame_desc_ = try_frame;

    if (negotiation_result_.dwClockFrequency != 0) {
        // This field is optional. If it isn't present, we instead
        // would use the default value provided in the video control header.
        clock_frequency_hz_ = negotiation_result_.dwClockFrequency;
    }

    zxlogf(INFO, "configured video: format index %u frame index %u\n",
           cur_format_->index, cur_frame_desc_->index);
    zxlogf(INFO, "alternate setting %d, packet size %u transactions per mf %u\n",
           cur_streaming_setting_->alt_setting,
           cur_streaming_setting_->max_packet_size,
           cur_streaming_setting_->transactions_per_microframe);

    return ZX_OK;
}

zx_status_t UsbVideoStream::TryFormat(const UsbVideoFormat* format,
                                      const UsbVideoFrameDesc* frame_desc,
                                      usb_video_vc_probe_and_commit_controls* out_result,
                                      const UsbVideoStreamingSetting** out_setting) {
    zxlogf(INFO, "trying format %u, frame desc %u\n",
           format->index, frame_desc->index);

    usb_video_vc_probe_and_commit_controls proposal;
    memset(&proposal, 0, sizeof(usb_video_vc_probe_and_commit_controls));
    proposal.bmHint = USB_VIDEO_BM_HINT_FRAME_INTERVAL;
    proposal.bFormatIndex = format->index;

    // Some formats do not have frame descriptors.
    if (frame_desc != NULL) {
        proposal.bFrameIndex = frame_desc->index;
        proposal.dwFrameInterval = frame_desc->default_frame_interval;
    }

    zx_status_t status = usb_video_negotiate_stream(
        &usb_, iface_num_, &proposal, out_result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_video_negotiate_stream failed: %d\n", status);
        return status;
    }

    // TODO(jocelyndang): we should calculate this ourselves instead
    // of reading the reported value, as it is incorrect in some devices.
    uint32_t required_bandwidth = negotiation_result_.dwMaxPayloadTransferSize;

    bool found = false;
    // Find a setting that supports the required bandwidth.
    for (const auto& setting : streaming_settings_) {
        uint32_t bandwidth = setting_bandwidth(setting);
        if (bandwidth >= required_bandwidth) {
            found = true;
            *out_setting = &setting;
            break;
        }
    }
    if (!found) {
        zxlogf(ERROR, "could not find a setting with bandwidth >= %u\n",
               required_bandwidth);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t UsbVideoStream::DdkIoctl(uint32_t op,
                                     const void* in_buf, size_t in_len,
                                     void* out_buf, size_t out_len, size_t* out_actual) {
    // The only IOCTL we support is get channel.
    if (op != CAMERA_IOCTL_GET_CHANNEL) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if ((out_buf == nullptr) ||
        (out_actual == nullptr) ||
        (out_len != sizeof(zx_handle_t))) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock lock(&lock_);

    if (stream_channel_ != nullptr) {
        // TODO(jocelyndang): support multiple concurrent clients.
        return ZX_ERR_ACCESS_DENIED;
    }

    auto channel = dispatcher::Channel::Create();
    if (channel == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    dispatcher::Channel::ProcessHandler phandler(
    [stream = this](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
        return stream->ProcessChannel(channel);
    });

    dispatcher::Channel::ChannelClosedHandler chandler(
    [stream = this](const dispatcher::Channel* channel) -> void {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
        stream->DeactivateStreamChannel(channel);
    });

    zx::channel client_endpoint;
    zx_status_t res = channel->Activate(&client_endpoint,
                                        default_domain_,
                                        fbl::move(phandler),
                                        fbl::move(chandler));
    if (res == ZX_OK) {
        stream_channel_ = channel;
        *(reinterpret_cast<zx_handle_t*>(out_buf)) = client_endpoint.release();
        *out_actual = sizeof(zx_handle_t);
    }
    return res;
}

#define HREQ(_cmd, _payload, _handler, ...)                     \
case _cmd:                                                      \
    if (req_size != sizeof(req._payload)) {                     \
        zxlogf(ERROR, "Bad " #_cmd                              \
                  " response length (%u != %zu)\n",             \
                  req_size, sizeof(req._payload));              \
        return ZX_ERR_INVALID_ARGS;                             \
    }                                                           \
    return _handler(channel, req._payload, ##__VA_ARGS__);

zx_status_t UsbVideoStream::ProcessChannel(dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(&lock_);

    union {
        camera::camera_proto::CmdHdr        hdr;
        camera::camera_proto::GetFormatsReq get_formats;
        camera::camera_proto::SetFormatReq  set_format;
    } req;

    static_assert(sizeof(req) <= 256,
                  "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK)
        return res;

    if (req_size < sizeof(req.hdr)) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch (req.hdr.cmd) {
    HREQ(CAMERA_STREAM_CMD_GET_FORMATS, get_formats, GetFormatsLocked);
    HREQ(CAMERA_STREAM_CMD_SET_FORMAT,  set_format,  SetFormatLocked);
    default:
        zxlogf(ERROR, "Unrecognized command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_ERR_NOT_SUPPORTED;
}
#undef HREQ


zx_status_t UsbVideoStream::GetFormatsLocked(dispatcher::Channel* channel,
                                             const camera::camera_proto::GetFormatsReq& req) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbVideoStream::SetFormatLocked(dispatcher::Channel* channel,
                                            const camera::camera_proto::SetFormatReq& req) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbVideoStream::RingBuffer::Init(uint32_t size) {
    zx_status_t status = zx::vmo::create(size, 0, &this->vmo);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to create ring buffer: %d\n", status);
        return status;
    }

    status = zx::vmar::root_self().map(0, this->vmo,
                                       0, size,
                                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                       reinterpret_cast<uintptr_t*>(&this->virt));
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to map ring buffer: %d\n", status);
        return status;
    }
    this->size = size;
    return ZX_OK;
}

zx_status_t UsbVideoStream::CreateDataRingBuffer() {
    fbl::AutoLock lock(&lock_);

    if (streaming_state_ != StreamingState::STOPPED) {
        return ZX_ERR_BAD_STATE;
    }
    // TODO(jocelyndang): figure out what to do for non frame based formats.
    uint32_t ring_buffer_size = RING_BUFFER_NUM_FRAMES * max_frame_size_;
    return data_ring_buffer_.Init(ring_buffer_size);
}

zx_status_t UsbVideoStream::StartStreaming() {
    fbl::AutoLock lock(&lock_);

    if (!data_ring_buffer_.virt || streaming_state_ != StreamingState::STOPPED) {
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = usb_set_interface(&usb_, iface_num_,
                                           cur_streaming_setting_->alt_setting);
    if (status != ZX_OK) {
        return status;
    }
    streaming_state_ = StreamingState::STARTED;

    while (!list_is_empty(&free_reqs_)) {
        QueueRequestLocked();
    }
    return ZX_OK;
}

zx_status_t UsbVideoStream::StopStreaming() {
    fbl::AutoLock lock(&lock_);

    if (streaming_state_ != StreamingState::STARTED) {
        return ZX_ERR_BAD_STATE;
    }
    // Need to wait for all the in-flight usb requests to complete
    // before we can be completely stopped.
    streaming_state_ = StreamingState::STOPPING;

    // Switch to the zero bandwidth alternate setting.
    zx_status_t status = usb_set_interface(&usb_, iface_num_, 0);
    if (status != ZX_OK) {
        return status;
    }
    return ZX_OK;
}

void UsbVideoStream::QueueRequestLocked() {
    auto req = list_remove_head_type(&free_reqs_, usb_request_t, node);
    ZX_DEBUG_ASSERT(req != nullptr);
    num_free_reqs_--;
    req->header.length = setting_bandwidth(*cur_streaming_setting_);
    usb_request_queue(&usb_, req);
}

void UsbVideoStream::RequestComplete(usb_request_t* req) {
    fbl::AutoLock lock(&lock_);

    if (streaming_state_ != StreamingState::STARTED) {
        // Stopped streaming so don't need to process the result.
        list_add_head(&free_reqs_, &req->node);
        num_free_reqs_++;
        if (num_free_reqs_ == num_allocated_reqs_) {
            zxlogf(TRACE, "setting ring buffer as stopped, got %u frames\n",
                   num_frames_);
            streaming_state_ = StreamingState::STOPPED;
        }
        return;
    }
    ProcessPayloadLocked(req);
    list_add_head(&free_reqs_, &req->node);
    num_free_reqs_++;
    QueueRequestLocked();
}

void UsbVideoStream::ParseHeaderTimestamps(usb_request_t* req) {
    // TODO(jocelyndang): handle other formats, the timestamp offset is variable.
    usb_video_vs_uncompressed_payload_header header = {};
    usb_request_copyfrom(req, &header,
                         sizeof(usb_video_vs_uncompressed_payload_header), 0);

    // PTS and STC should stay the same for payloads of the same frame,
    // but it's probably not a critical error if they're different.

    if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_PTS) {
        uint32_t new_pts = header.dwPresentationTime;

        if (cur_frame_pts_ != 0 && new_pts != cur_frame_pts_) {
            zxlogf(ERROR, "#%u: PTS changed between payloads, from %u to %u\n",
            num_frames_, cur_frame_pts_, new_pts);
        }
        cur_frame_pts_ = new_pts;
    }

    if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_SCR) {
        uint32_t new_stc = header.scrSourceTimeClock;

        if (cur_frame_stc_ != 0 && new_stc != cur_frame_stc_) {
            zxlogf(ERROR, "#%u: STC changed between payloads, from %u to %u\n",
            num_frames_, cur_frame_stc_, new_stc);
        }
        cur_frame_stc_ = new_stc;
    }
}

void UsbVideoStream::ProcessPayloadLocked(usb_request_t* req) {
    if (req->response.status != ZX_OK) {
        zxlogf(ERROR, "usb request failed: %d\n", req->response.status);
        return;
    }
    // Empty responses should be ignored.
    if (req->response.actual == 0) {
        return;
    }
    // Different payload types have different header types but always share
    // the same first two bytes.
    usb_video_vs_payload_header header;
    size_t len = usb_request_copyfrom(req, &header,
                                      sizeof(usb_video_vs_payload_header), 0);

    if (len != sizeof(usb_video_vs_payload_header) ||
        header.bHeaderLength > req->response.actual) {
        zxlogf(ERROR, "got invalid header bHeaderLength %u data length %lu\n",
               header.bHeaderLength, req->response.actual);
        return;
    }

    uint8_t fid = header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_FID;
    // FID is toggled when a new frame begins.
    if (cur_fid_ != fid) {
        // Only advance the ring buffer position if the frame had no errors.
        // TODO(jocelyndang): figure out if we should do something else.
        if (!cur_frame_error_) {
            data_ring_buffer_.offset += cur_frame_bytes_;
            if (data_ring_buffer_.offset >= data_ring_buffer_.size) {
                data_ring_buffer_.offset -= data_ring_buffer_.size;
                ZX_DEBUG_ASSERT(data_ring_buffer_.offset < data_ring_buffer_.size);
            }
        }

        if (clock_frequency_hz_ != 0) {
            zxlogf(TRACE, "#%u: PTS = %lfs, STC = %lfs\n",
                   num_frames_,
                   cur_frame_pts_ / static_cast<double>(clock_frequency_hz_),
                   cur_frame_stc_ / static_cast<double>(clock_frequency_hz_));
        }

        num_frames_++;
        cur_frame_bytes_ = 0;
        cur_frame_error_ = false;
        cur_fid_ = fid;
        cur_frame_pts_ = 0;
        cur_frame_stc_ = 0;
    }
    if (cur_frame_error_) {
        zxlogf(ERROR, "skipping payload of invalid frame #%u\n", num_frames_);
        return;
    }
    if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_ERR) {
        zxlogf(ERROR, "payload of frame #%u had an error bit set\n", num_frames_);
        cur_frame_error_ = true;
        return;
    }

    ParseHeaderTimestamps(req);

    // Copy the data into the ring buffer;
    uint32_t offset = header.bHeaderLength;
    uint32_t data_size = static_cast<uint32_t>(req->response.actual) - offset;
    if (cur_frame_bytes_ + data_size > max_frame_size_) {
        zxlogf(ERROR, "invalid data size %u, cur frame bytes %u, frame size %u\n",
               data_size, cur_frame_bytes_, max_frame_size_);
        cur_frame_error_ = true;
        return;
    }

    // Append the data to the end of the current frame.
    uint32_t frame_end_offset = data_ring_buffer_.offset + cur_frame_bytes_;
    if (frame_end_offset >= data_ring_buffer_.size) {
        frame_end_offset -= data_ring_buffer_.size;
    }

    uint32_t avail = data_ring_buffer_.size - frame_end_offset;
    ZX_DEBUG_ASSERT(frame_end_offset < data_ring_buffer_.size);

    uint32_t amt = fbl::min(avail, data_size);
    uint8_t* dst = reinterpret_cast<uint8_t*>(data_ring_buffer_.virt) + frame_end_offset;

    usb_request_copyfrom(req, dst, amt, offset);
    if (amt < data_size) {
        usb_request_copyfrom(req, data_ring_buffer_.virt, data_size - amt, offset + amt);
    }
    cur_frame_bytes_ += data_size;
}

void UsbVideoStream::DeactivateStreamChannel(const dispatcher::Channel* channel) {
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() == channel);
    stream_channel_.reset();
}

void UsbVideoStream::DdkUnbind() {
    default_domain_->Deactivate();

    // Unpublish our device node.
    DdkRemove();
}

void UsbVideoStream::DdkRelease() {
    delete this;
}

} // namespace usb
} // namespace video
