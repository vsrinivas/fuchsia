// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
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
static constexpr uint32_t NANOSECS_IN_SEC = 1e9;

UsbVideoStream::~UsbVideoStream() {
    // List may not have been initialized.
    if (free_reqs_.next) {
        while (!list_is_empty(&free_reqs_)) {
            usb_request_release(list_remove_head_type(&free_reqs_, usb_request_t, node));
        }
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
        !formats || formats->size() == 0 || !settings || settings->size() == 0) {
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

        // The streaming settings should all be of the same type,
        // either all USB_ENDPOINT_BULK or all USB_ENDPOINT_ISOCHRONOUS.
        if (streaming_ep_type_ != USB_ENDPOINT_INVALID &&
            streaming_ep_type_ != setting.ep_type) {
            zxlogf(ERROR, "mismatched EP types: %u and %u\n",
                   streaming_ep_type_, setting.ep_type);
            return ZX_ERR_BAD_STATE;
        }
        streaming_ep_type_ = setting.ep_type;
    }

    // A video streaming interface containing a bulk endpoint for streaming
    // shall support only alternate setting zero.
    if (streaming_ep_type_ == USB_ENDPOINT_BULK &&
        (streaming_settings_.size() > 1 || streaming_settings_.get()->alt_setting != 0)) {
        zxlogf(ERROR, "invalid streaming settings for bulk endpoint\n");
        return ZX_ERR_BAD_STATE;
    }

    {
        fbl::AutoLock lock(&lock_);

        list_initialize(&free_reqs_);

        // For isochronous transfers we know the maximum payload size to
        // use for the usb request size.
        //
        // For bulk transfers we can't allocate usb requests until we get
        // the maximum payload size from stream negotiation.
        if (streaming_ep_type_ == USB_ENDPOINT_ISOCHRONOUS) {
            zx_status_t status = AllocUsbRequestsLocked(max_bandwidth);
            if (status != ZX_OK) {
                return status;
            }
        }
    }

    zx_status_t status = GenerateFormatMappings();
    if (status != ZX_OK) {
        return status;
    }

    status = UsbVideoStreamBase::DdkAdd(devname, DEVICE_ADD_INVISIBLE);
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
    CreateDataVideoBuffer();
    StartStreaming();
    return ZX_OK;
}

UsbVideoStream::FormatMapping::FormatMapping(const UsbVideoFormat* format,
                                             const UsbVideoFrameDesc* frame_desc) {
    this->proto.capture_type = frame_desc->capture_type;
    this->proto.pixel_format = format->pixel_format;
    this->proto.width = frame_desc->width;
    this->proto.height = frame_desc->height;
    this->proto.stride = frame_desc->stride;
    this->proto.bits_per_pixel = format->bits_per_pixel;
    // The frame descriptor frame interval is expressed in 100ns units.
    // e.g. a frame interval of 333333 is equivalent to 30fps (1e7 / 333333).
    this->proto.frames_per_sec_numerator = NANOSECS_IN_SEC / 100;
    this->proto.frames_per_sec_denominator = frame_desc->default_frame_interval;

    this->format = format;
    this->frame_desc = frame_desc;
}

zx_status_t UsbVideoStream::GenerateFormatMappings() {
    size_t num_mappings = 0;
    for (const auto& format : formats_) {
        num_mappings += format.frame_descs.size();
    }

    // The camera interface limits the number of formats we can send to the
    // client, so flag an error early in case this ever happens.
    if (num_mappings > fbl::numeric_limits<uint16_t>::max()) {
        zxlogf(ERROR, "too many format mappings (%lu count)\n", num_mappings);
        return ZX_ERR_INTERNAL;
    }

    fbl::AllocChecker ac;
    this->format_mappings_.reserve(num_mappings, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    for (const auto& format : formats_) {
        for (const auto& frame : format.frame_descs) {
            format_mappings_.push_back(FormatMapping(&format, &frame));
        }
    }
    return ZX_OK;
}

zx_status_t UsbVideoStream::SetFormat() {
    fbl::AutoLock lock(&lock_);

    if (streaming_state_ != StreamingState::STOPPED) {
        // TODO(jocelyndang): stop the video buffer rather than returning an error.
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
    zx_status_t status = TryFormatLocked(format, try_frame);
    if (status != ZX_OK) {
        // Negotiation failed. Try a different frame descriptor.
        for (const auto& frame : format->frame_descs) {
            if (frame.index == format->default_frame_index) {
                // Already tried this setting.
                continue;
            }
            try_frame = &frame;
            status = TryFormatLocked(format, try_frame);
            if (status == ZX_OK) {
                break;
            }
        }
    }
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to set format %u: error %d\n", format->index, status);
        return status;
    }
    return ZX_OK;
}

zx_status_t UsbVideoStream::AllocUsbRequestsLocked(uint64_t size) {
    if (streaming_state_ != StreamingState::STOPPED) {
        return ZX_ERR_BAD_STATE;
    }
    if (size <= allocated_req_size_) {
        // Can reuse existing usb requests.
       return ZX_OK;
    }
    // Need to allocate new usb requests, release any existing ones.
    while (!list_is_empty(&free_reqs_)) {
        usb_request_release(list_remove_head_type(&free_reqs_, usb_request_t, node));
    }

    zxlogf(TRACE, "allocating %d usb requests of size %lu\n",
           MAX_OUTSTANDING_REQS, size);

    for (uint32_t i = 0; i < MAX_OUTSTANDING_REQS; i++) {
        usb_request_t* req;
        zx_status_t status = usb_request_alloc(&req,
                                               size,
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
    allocated_req_size_ = size;
    return ZX_OK;
}

zx_status_t UsbVideoStream::TryFormatLocked(const UsbVideoFormat* format,
                                            const UsbVideoFrameDesc* frame_desc) {
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

    usb_video_vc_probe_and_commit_controls result;
    zx_status_t status = usb_video_negotiate_probe(
        &usb_, iface_num_, &proposal, &result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_video_negotiate_probe failed: %d\n", status);
        return status;
    }

    // TODO(jocelyndang): we should calculate this ourselves instead
    // of reading the reported value, as it is incorrect in some devices.
    uint32_t required_bandwidth = result.dwMaxPayloadTransferSize;

    const UsbVideoStreamingSetting* best_setting = nullptr;
    // Find a setting that supports the required bandwidth.
    for (const auto& setting : streaming_settings_) {
        uint32_t bandwidth = setting_bandwidth(setting);
        // For bulk transfers, we use the first (and only) setting.
        if (setting.ep_type == USB_ENDPOINT_BULK ||
            bandwidth >= required_bandwidth) {
            best_setting = &setting;
            break;
        }
    }
    if (!best_setting) {
        zxlogf(ERROR, "could not find a setting with bandwidth >= %u\n",
               required_bandwidth);
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = usb_video_negotiate_commit(&usb_, iface_num_, &result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_video_negotiate_commit failed: %d\n", status);
        return status;
    }

    // Negotiation succeeded, copy the results out.
    memcpy(&negotiation_result_, &result, sizeof(usb_video_vc_probe_and_commit_controls));
    cur_streaming_setting_ = best_setting;

    max_frame_size_ = negotiation_result_.dwMaxVideoFrameSize;
    cur_format_ = format;
    cur_frame_desc_ = frame_desc;

    if (negotiation_result_.dwClockFrequency != 0) {
        // This field is optional. If it isn't present, we instead
        // would use the default value provided in the video control header.
        clock_frequency_hz_ = negotiation_result_.dwClockFrequency;
    }

    switch(streaming_ep_type_) {
    case USB_ENDPOINT_ISOCHRONOUS:
        // Isochronous payloads will always fit within a single usb request.
        send_req_size_ = setting_bandwidth(*cur_streaming_setting_);
        break;
    case USB_ENDPOINT_BULK: {
        // If the size of a payload is greater than the max usb request size,
        // we will have to split it up in multiple requests.
        send_req_size_ = fbl::min(usb_get_max_transfer_size(&usb_, usb_ep_addr_),
            static_cast<uint64_t>(negotiation_result_.dwMaxPayloadTransferSize));
        break;
    }
    default:
       zxlogf(ERROR, "unknown EP type: %d\n", streaming_ep_type_);
       return ZX_ERR_BAD_STATE;
    }

    zxlogf(INFO, "configured video: format index %u frame index %u\n",
           cur_format_->index, cur_frame_desc_->index);
    zxlogf(INFO, "alternate setting %d, packet size %u transactions per mf %u\n",
           cur_streaming_setting_->alt_setting,
           cur_streaming_setting_->max_packet_size,
           cur_streaming_setting_->transactions_per_microframe);

    return AllocUsbRequestsLocked(send_req_size_);
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
    camera::camera_proto::GetFormatsResp resp = { };
    resp.hdr = req.hdr;
    resp.total_format_count = static_cast<uint16_t>(format_mappings_.size());

    // Each channel message is limited in the number of formats it can hold,
    // so we may have to send several messages.
    size_t cur_send_count = fbl::min<size_t>(
        format_mappings_.size(),
        CAMERA_STREAM_CMD_GET_FORMATS_MAX_FORMATS_PER_RESPONSE);

    size_t copied_count = 0;
    for (const auto& mapping : format_mappings_) {
        memcpy(&resp.formats[copied_count], &mapping.proto,
               sizeof(camera::camera_proto::VideoFormat));
        copied_count++;

        // We've filled up the messages' formats array, time to send the message.
        if (copied_count == cur_send_count) {
           zx_status_t res = channel->Write(&resp, sizeof(resp));
           if (res != ZX_OK) {
               zxlogf(ERROR, "writing formats to channel failed, err: %d\n", res);
               return res;
           }

           resp.already_sent_count =
               static_cast<uint16_t>(resp.already_sent_count + cur_send_count);
           cur_send_count = fbl::min<size_t>(
               format_mappings_.size() - resp.already_sent_count,
               CAMERA_STREAM_CMD_GET_FORMATS_MAX_FORMATS_PER_RESPONSE);
           copied_count = 0;
        }
    }
    return ZX_OK;
}

zx_status_t UsbVideoStream::SetFormatLocked(dispatcher::Channel* channel,
                                            const camera::camera_proto::SetFormatReq& req) {
    return ZX_ERR_NOT_SUPPORTED;
}

// TODO(jocelyndang): this is only for temporary testing purposes and can be
// removed once we have SET_BUFFER implemented.
zx_status_t UsbVideoStream::CreateDataVideoBuffer() {
    fbl::AutoLock lock(&lock_);

    if (streaming_state_ != StreamingState::STOPPED) {
        return ZX_ERR_BAD_STATE;
    }
    // TODO(jocelyndang): figure out what to do for non frame based formats.
    uint32_t video_buffer_size = RING_BUFFER_NUM_FRAMES * max_frame_size_;

    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(video_buffer_size, 0, &vmo);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to create video buffer: %d\n", status);
        return status;
    }
    return VideoBuffer::Create(fbl::move(vmo), &video_buffer_);
}

zx_status_t UsbVideoStream::StartStreaming() {
    fbl::AutoLock lock(&lock_);

    if (!video_buffer_ || !video_buffer_->virt() ||
        streaming_state_ != StreamingState::STOPPED) {
        return ZX_ERR_BAD_STATE;
    }

    // Initialize the state.
    num_frames_ = 0;
    cur_frame_state_ = {};
    // FID of the first seen frame could either be 0 or 1.
    // Initialize this to -1 so that the first frame will consistently be
    // detected as a new frame.
    cur_frame_state_.fid = -1;

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
    req->header.length = send_req_size_;
    usb_request_queue(&usb_, req);
}

void UsbVideoStream::RequestComplete(usb_request_t* req) {
    fbl::AutoLock lock(&lock_);

    if (streaming_state_ != StreamingState::STARTED) {
        // Stopped streaming so don't need to process the result.
        list_add_head(&free_reqs_, &req->node);
        num_free_reqs_++;
        if (num_free_reqs_ == num_allocated_reqs_) {
            zxlogf(TRACE, "setting video buffer as stopped, got %u frames\n",
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

        if (cur_frame_state_.pts != 0 && new_pts != cur_frame_state_.pts) {
            zxlogf(ERROR, "#%u: PTS changed between payloads, from %u to %u\n",
            num_frames_, cur_frame_state_.pts, new_pts);
        }
        cur_frame_state_.pts = new_pts;
    }

    if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_SCR) {
        uint32_t new_stc = header.scrSourceTimeClock;

        if (cur_frame_state_.stc != 0 && new_stc != cur_frame_state_.stc) {
            zxlogf(ERROR, "#%u: STC changed between payloads, from %u to %u\n",
            num_frames_, cur_frame_state_.stc, new_stc);
        }
        cur_frame_state_.stc = new_stc;
    }
}

zx_status_t UsbVideoStream::ParsePayloadHeaderLocked(usb_request_t* req,
                                                     uint32_t* out_header_length) {
    // Different payload types have different header types but always share
    // the same first two bytes.
    usb_video_vs_payload_header header;
    size_t len = usb_request_copyfrom(req, &header,
                                      sizeof(usb_video_vs_payload_header), 0);

    if (len != sizeof(usb_video_vs_payload_header) ||
        header.bHeaderLength > req->response.actual) {
        zxlogf(ERROR, "got invalid header bHeaderLength %u data length %lu\n",
               header.bHeaderLength, req->response.actual);
        return ZX_ERR_INTERNAL;
    }

    uint8_t fid = header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_FID;
    // FID is toggled when a new frame begins. This means any in progress frame
    // is now complete, and we are currently parsing the header of a new frame.
    if (cur_frame_state_.fid != fid) {
        // If the currently stored FID is valid, we've just completed a frame.
        if (cur_frame_state_.fid >= 0) {
            // Only advance the video buffer position if the frame had no errors.
            // TODO(jocelyndang): figure out if we should do something else.
            if (!cur_frame_state_.error) {
                // TODO(jocelyndang): ask video_buffer_ for the next available
                // offset rather than just incrementing here.
                video_buffer_offset_ += max_frame_size_;
                if (video_buffer_offset_ >= video_buffer_->size()) {
                    video_buffer_offset_ -= video_buffer_->size();
                    ZX_DEBUG_ASSERT(video_buffer_offset_ < video_buffer_->size());
                }
            }

            if (clock_frequency_hz_ != 0) {
                zxlogf(TRACE, "#%u: PTS = %lfs, STC = %lfs\n",
                       num_frames_,
                       cur_frame_state_.pts / static_cast<double>(clock_frequency_hz_),
                       cur_frame_state_.stc / static_cast<double>(clock_frequency_hz_));
            }
        }

        // Initialize the frame state for the new frame.
        cur_frame_state_ = {};
        cur_frame_state_.fid = fid;
        num_frames_++;
    }
    if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_ERR) {
        // Only print the error message for the first erroneous payload of the
        // frame.
        if (!cur_frame_state_.error) {
            zxlogf(ERROR, "payload of frame #%u had an error bit set\n",
                   num_frames_);
            cur_frame_state_.error = true;
        }
        return ZX_OK;
    }

    ParseHeaderTimestamps(req);

    *out_header_length = header.bHeaderLength;
    return ZX_OK;
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

    bool is_bulk = streaming_ep_type_ == USB_ENDPOINT_BULK;
    uint32_t header_len = 0;
    // Each isochronous response contains a payload header.
    // For bulk responses, a payload may be split over several requests,
    // so only parse the header if it's the first request of the payload.
    if (!is_bulk || bulk_payload_bytes_ == 0) {
        zx_status_t status = ParsePayloadHeaderLocked(req, &header_len);
        if (status != ZX_OK) {
            return;
        }
    }
    // End of payload detection for bulk transfers.
    // Unlike isochronous transfers, we aren't guaranteed a payload header
    // per usb response. To detect the end of a payload, we need to check
    // whether we've read enough bytes.
    if (is_bulk) {
        // We need to update the total bytes counter before checking the error field,
        // otherwise we might return early and start of payload detection will be wrong.
        bulk_payload_bytes_ += static_cast<uint32_t>(req->response.actual);
        // A payload is complete when we've received enough bytes to reach the max
        // payload size, or fewer bytes than what we requested.
        if (bulk_payload_bytes_ >= negotiation_result_.dwMaxPayloadTransferSize ||
            req->response.actual < send_req_size_) {
            bulk_payload_bytes_ = 0;
        }
    }

    if (cur_frame_state_.error) {
        zxlogf(TRACE, "skipping payload of invalid frame #%u\n", num_frames_);
        return;
    }

    // Copy the data into the video buffer.
    uint32_t data_size = static_cast<uint32_t>(req->response.actual) - header_len;
    if (cur_frame_state_.bytes + data_size > max_frame_size_) {
        zxlogf(ERROR, "invalid data size %u, cur frame bytes %u, frame size %u\n",
               data_size, cur_frame_state_.bytes, max_frame_size_);
        cur_frame_state_.error = true;
        return;
    }

    // Append the data to the end of the current frame.
    uint64_t frame_end_offset = video_buffer_offset_ + cur_frame_state_.bytes;
    ZX_DEBUG_ASSERT(frame_end_offset <= video_buffer_->size());

    uint64_t avail = video_buffer_->size() - frame_end_offset;
    ZX_DEBUG_ASSERT(avail >= data_size);

    uint8_t* dst = reinterpret_cast<uint8_t*>(video_buffer_->virt()) + frame_end_offset;
    usb_request_copyfrom(req, dst, data_size, header_len);

    cur_frame_state_.bytes += data_size;
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
