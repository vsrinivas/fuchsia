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
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/zx/vmar.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/usb.h>

#include "garnet/drivers/usb_video/camera_control_impl.h"
#include "garnet/drivers/usb_video/usb-video-stream.h"
#include "garnet/drivers/usb_video/video-util.h"

namespace video {
namespace usb {

static constexpr uint32_t MAX_OUTSTANDING_REQS = 8;

// Only keep the first 11 bits of the USB SOF (Start of Frame) values.
// The payload header SOF values only have 11 bits before wrapping around,
// whereas the XHCI host returns 64 bits.
static constexpr uint16_t USB_SOF_MASK = 0x7FF;

fbl::unique_ptr<async::Loop> UsbVideoStream::fidl_dispatch_loop_ = nullptr;

UsbVideoStream::UsbVideoStream(zx_device_t* parent, usb_protocol_t* usb,
                               UvcFormatList format_list,
                               fbl::Vector<UsbVideoStreamingSetting>* settings)
    : UsbVideoStreamBase(parent),
      usb_(*usb),
      format_list_(fbl::move(format_list)),
      streaming_settings_(fbl::move(*settings)) {
  if (fidl_dispatch_loop_ == nullptr) {
    fidl_dispatch_loop_ =
        fbl::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
    fidl_dispatch_loop_->StartThread();
  }
}

UsbVideoStream::~UsbVideoStream() {
  // List may not have been initialized.
  if (free_reqs_.next) {
    while (!list_is_empty(&free_reqs_)) {
      usb_req_release(&usb_,
                      list_remove_head_type(&free_reqs_, usb_request_t, node));
    }
  }
}

// static
zx_status_t UsbVideoStream::Create(
    zx_device_t* device, usb_protocol_t* usb, int index,
    usb_interface_descriptor_t* intf, usb_video_vc_header_desc* control_header,
    usb_video_vs_input_header_desc* input_header, UvcFormatList format_list,
    fbl::Vector<UsbVideoStreamingSetting>* settings) {
  if (!usb || !intf || !control_header || !input_header || !settings ||
      settings->size() == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto dev = fbl::unique_ptr<UsbVideoStream>(
      new UsbVideoStream(device, usb, std::move(format_list), settings));

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
      zxlogf(ERROR, "mismatched EP types: %u and %u\n", streaming_ep_type_,
             setting.ep_type);
      return ZX_ERR_BAD_STATE;
    }
    streaming_ep_type_ = setting.ep_type;
  }

  // A video streaming interface containing a bulk endpoint for streaming
  // shall support only alternate setting zero.
  if (streaming_ep_type_ == USB_ENDPOINT_BULK &&
      (streaming_settings_.size() > 1 ||
       streaming_settings_.get()->alt_setting != 0)) {
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
  return UsbVideoStreamBase::DdkAdd(devname);
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
    usb_req_release(&usb_,
                    list_remove_head_type(&free_reqs_, usb_request_t, node));
  }

  zxlogf(TRACE, "allocating %d usb requests of size %lu\n",
         MAX_OUTSTANDING_REQS, size);

  for (uint32_t i = 0; i < MAX_OUTSTANDING_REQS; i++) {
    usb_request_t* req;
    zx_status_t status = usb_req_alloc(&usb_, &req, size, usb_ep_addr_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "usb_req_alloc failed: %d\n", status);
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

zx_status_t UsbVideoStream::TryFormatLocked(uint8_t format_index,
                                            uint8_t frame_index,
                                            uint32_t default_frame_interval) {
  zxlogf(INFO, "trying format %u, frame desc %u\n", format_index, frame_index);

  usb_video_vc_probe_and_commit_controls proposal;
  memset(&proposal, 0, sizeof(usb_video_vc_probe_and_commit_controls));
  proposal.bmHint = USB_VIDEO_BM_HINT_FRAME_INTERVAL;
  proposal.bFormatIndex = format_index;

  // TODO(garratt): Some formats do not have frame descriptors.
  proposal.bFrameIndex = frame_index;
  proposal.dwFrameInterval = default_frame_interval;

  usb_video_vc_probe_and_commit_controls result;
  zx_status_t status =
      usb_video_negotiate_probe(&usb_, iface_num_, &proposal, &result);
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
  memcpy(&negotiation_result_, &result,
         sizeof(usb_video_vc_probe_and_commit_controls));
  cur_streaming_setting_ = best_setting;

  // Round frame size up to a whole number of pages, to allow mapping the frames
  // individually to vmars.
  max_frame_size_ = ROUNDUP(negotiation_result_.dwMaxVideoFrameSize, PAGE_SIZE);

  if (negotiation_result_.dwClockFrequency != 0) {
    // This field is optional. If it isn't present, we instead
    // would use the default value provided in the video control header.
    clock_frequency_hz_ = negotiation_result_.dwClockFrequency;
  }

  switch (streaming_ep_type_) {
    case USB_ENDPOINT_ISOCHRONOUS:
      // Isochronous payloads will always fit within a single usb request.
      send_req_size_ = setting_bandwidth(*cur_streaming_setting_);
      break;
    case USB_ENDPOINT_BULK: {
      // If the size of a payload is greater than the max usb request size,
      // we will have to split it up in multiple requests.
      send_req_size_ = fbl::min(
          usb_get_max_transfer_size(&usb_, usb_ep_addr_),
          static_cast<uint64_t>(negotiation_result_.dwMaxPayloadTransferSize));
      break;
    }
    default:
      zxlogf(ERROR, "unknown EP type: %d\n", streaming_ep_type_);
      return ZX_ERR_BAD_STATE;
  }

  zxlogf(INFO, "configured video: format index %u frame index %u\n",
         format_index, frame_index);
  zxlogf(INFO, "alternate setting %d, packet size %u transactions per mf %u\n",
         cur_streaming_setting_->alt_setting,
         cur_streaming_setting_->max_packet_size,
         cur_streaming_setting_->transactions_per_microframe);

  return AllocUsbRequestsLocked(send_req_size_);
}

zx_status_t UsbVideoStream::DdkIoctl(uint32_t op, const void* in_buf,
                                     size_t in_len, void* out_buf,
                                     size_t out_len, size_t* out_actual) {
  // The only IOCTL we support is get channel.
  if (op != CAMERA_IOCTL_GET_CHANNEL) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if ((out_buf == nullptr) || (out_actual == nullptr) ||
      (out_len != sizeof(zx_handle_t))) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);

  if (camera_control_ != nullptr) {
    zxlogf(ERROR, "Camera Control already running\n");
    // TODO(CAM-XXX): support multiple concurrent clients.
    return ZX_ERR_ACCESS_DENIED;
  }

  fidl::InterfaceHandle<fuchsia::camera::driver::Control> control_handle;
  fidl::InterfaceRequest<fuchsia::camera::driver::Control> control_interface =
      control_handle.NewRequest();

  if (control_interface.is_valid()) {
    camera_control_ = fbl::make_unique<camera::ControlImpl>(
        this, fbl::move(control_interface), fidl_dispatch_loop_->dispatcher(),
        [this] {
          fbl::AutoLock lock(&lock_);

          camera_control_.reset();
        });

    *(reinterpret_cast<zx_handle_t*>(out_buf)) =
        control_handle.TakeChannel().release();
    *out_actual = sizeof(zx_handle_t);

    return ZX_OK;
  } else {
    return ZX_ERR_NO_RESOURCES;
  }
}

zx_status_t UsbVideoStream::GetFormats(
    fidl::VectorPtr<fuchsia::camera::driver::VideoFormat>& formats) {
  fbl::AutoLock lock(&lock_);
  format_list_.FillFormats(formats);
  return ZX_OK;
}

zx_status_t UsbVideoStream::SetFormat(
    const fuchsia::camera::driver::VideoFormat& video_format,
    uint32_t* max_frame_size) {
  fbl::AutoLock lock(&lock_);

  // Convert from the client's video format proto to the device driver format
  // and frame descriptors.
  uint8_t format_index, frame_index;
  uint32_t default_frame_interval;
  bool is_matched = format_list_.MatchFormat(
      video_format, &format_index, &frame_index, &default_frame_interval);
  if (!is_matched) {
    zxlogf(ERROR, "could not find a mapping for the requested format\n");
    return ZX_ERR_NOT_FOUND;
  }

  if (streaming_state_ != StreamingState::STOPPED) {
    zxlogf(ERROR, "cannot set video format while streaming is not stopped\n");
    return ZX_ERR_BAD_STATE;
  }

  // Try setting the format on the device.
  zx_status_t status =
      TryFormatLocked(format_index, frame_index, default_frame_interval);
  if (status != ZX_OK) {
    zxlogf(ERROR, "setting format failed, err: %d\n", status);
    return status;
  }

  *max_frame_size = max_frame_size_;

  return ZX_OK;
}

zx_status_t UsbVideoStream::SetBuffer(zx::vmo buffer) {
  fbl::AutoLock lock(&lock_);

  if (streaming_state_ != StreamingState::STOPPED) {
    return ZX_ERR_BAD_STATE;
  }

  if (!buffer.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }

  // Release any previously stored video buffer.
  video_buffer_.reset();

  zx_status_t res =
      VideoBuffer::Create(fbl::move(buffer), &video_buffer_, max_frame_size_);

  return res;
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
  bulk_payload_bytes_ = 0;
  video_buffer_->Init();

  zx_status_t status =
      usb_set_interface(&usb_, iface_num_, cur_streaming_setting_->alt_setting);
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
  // We won't send the stop response until then.
  streaming_state_ = StreamingState::STOPPING;

  // Switch to the zero bandwidth alternate setting.
  return usb_set_interface(&usb_, iface_num_, 0);
}

zx_status_t UsbVideoStream::FrameRelease(uint64_t frame_offset) {
  fbl::AutoLock lock(&lock_);
  return video_buffer_->FrameRelease(frame_offset);
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

      if (camera_control_) {
        camera_control_->Stopped();
      }
    }
    return;
  }
  ProcessPayloadLocked(req);
  list_add_head(&free_reqs_, &req->node);
  num_free_reqs_++;
  QueueRequestLocked();
}

// Converts from device clock units to milliseconds.
static inline double device_clock_to_ms(uint32_t clock_reading,
                                        uint32_t clock_frequency_hz) {
  return clock_frequency_hz != 0 ? clock_reading * 1000.0 / clock_frequency_hz
                                 : 0;
}

void UsbVideoStream::ParseHeaderTimestamps(usb_request_t* req) {
  // TODO(jocelyndang): handle other formats, the timestamp offset is variable.
  usb_video_vs_uncompressed_payload_header header = {};
  usb_req_copy_from(&usb_, req, &header,
                    sizeof(usb_video_vs_uncompressed_payload_header), 0);

  // PTS should stay the same for payloads of the same frame,
  // but it's probably not a critical error if they're different.
  if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_PTS) {
    uint32_t new_pts = header.dwPresentationTime;

    // Use the first seen PTS value.
    if (cur_frame_state_.pts == 0) {
      cur_frame_state_.pts = new_pts;
    } else if (new_pts != cur_frame_state_.pts) {
      zxlogf(ERROR, "#%u: PTS changed between payloads, from %u to %u\n",
             num_frames_, cur_frame_state_.pts, new_pts);
    }
  }

  if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_SCR) {
    uint32_t new_stc = header.scrSourceTimeClock;
    uint16_t new_sof = header.scrSourceClockSOFCounter;

    // The USB Video Class Spec 1.1 suggests that updated SCR values may
    // be provided per payload of a frame. Only use the first seen value.
    if (cur_frame_state_.stc == 0) {
      cur_frame_state_.stc = new_stc;
      cur_frame_state_.device_sof = new_sof;
    }
  }

  // The device might not support header timestamps.
  if (cur_frame_state_.pts == 0 || cur_frame_state_.stc == 0) {
    return;
  }
  // Already calculated the capture time for the frame.
  if (cur_frame_state_.capture_time != 0) {
    return;
  }

  // Calculate the capture time. This uses the method detailed in the
  // USB Video Class 1.5 FAQ, Section 2.7 Audio and Video Stream
  // Synchronization.
  //
  //  Event                      Available Timestamps
  //  ------------------------   ----------------------------------
  //  raw frame capture starts - PTS in device clock units
  //  raw frame capture ends   - STC in device clock units, device SOF
  //  driver receives frame    - host monotonic timestamp, host SOF
  //
  // TODO(jocelyndang): revisit this. This may be slightly inaccurate for
  // devices implementing the 1.1 version of the spec, which states that a
  // payload's SOF number is not required to match the 'current' frame number.

  // Get the current host SOF value and host monotonic timestamp.
  size_t len;
  zx_status_t status = device_ioctl(parent_, IOCTL_USB_GET_CURRENT_FRAME, NULL,
                                    0, &cur_frame_state_.host_sof,
                                    sizeof(cur_frame_state_.host_sof), &len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not get host SOF, err: %d\n", status);
    return;
  }
  zx_time_t host_complete_time_ns = zx_clock_get_monotonic();

  // Calculate the difference between when raw frame capture starts and ends.
  uint32_t device_delay = cur_frame_state_.stc - cur_frame_state_.pts;
  double device_delay_ms =
      device_clock_to_ms(device_delay, clock_frequency_hz_);

  // Calculate the delay caused by USB transport and processing. This will be
  // the time between raw frame capture ending and the driver receiving the
  // frame
  //
  // SOF (Start of Frame) values are transmitted by the USB host every
  // millisecond.
  // We want the difference between the SOF values of when frame capture
  // completed (device_sof) and when we received the frame (host_sof).
  //
  // Since the device SOF value only has 11 bits and wraps around, we should
  // discard the higher bits of the result. The delay is expected to be
  // less than 2^11 ms.
  uint16_t transport_delay_ms =
      (cur_frame_state_.host_sof - cur_frame_state_.device_sof) & USB_SOF_MASK;

  // Time between when raw frame capture starts and the driver receiving the
  // frame.
  double total_video_delay = device_delay_ms + transport_delay_ms;

  // Start of raw frame capture as zx_time_t (nanoseconds).
  zx_time_t capture_start_ns =
      host_complete_time_ns - ZX_MSEC(total_video_delay);
  // The capture time is specified in the camera interface as the midpoint of
  // the capture operation, not including USB transport time.
  cur_frame_state_.capture_time =
      capture_start_ns + ZX_MSEC(device_delay_ms) / 2;
}

zx_status_t UsbVideoStream::FrameNotifyLocked() {
  if (clock_frequency_hz_ != 0) {
    zxlogf(TRACE,
           "#%u: [%ld ns] PTS = %lfs, STC = %lfs, SOF = %u host SOF = %lu\n",
           num_frames_, cur_frame_state_.capture_time,
           cur_frame_state_.pts / static_cast<double>(clock_frequency_hz_),
           cur_frame_state_.stc / static_cast<double>(clock_frequency_hz_),
           cur_frame_state_.device_sof, cur_frame_state_.host_sof);
  }

  if (camera_control_ == nullptr) {
    // Can't send a notification if there's no channel.
    return ZX_OK;
  }

  fuchsia::camera::driver::FrameAvailableEvent event = {};
  event.metadata.timestamp = cur_frame_state_.capture_time;

  if (cur_frame_state_.error) {
    event.frame_status = fuchsia::camera::driver::FrameStatus::ERROR_FRAME;

  } else if (!has_video_buffer_offset_) {
    event.frame_status =
        fuchsia::camera::driver::FrameStatus::ERROR_BUFFER_FULL;

    // Only mark the frame completed if it had no errors and had data stored.
  } else if (cur_frame_state_.bytes > 0) {
    event.frame_size = cur_frame_state_.bytes;
    event.frame_offset = video_buffer_offset_;

    // Need to lock the frame before sending the notification.
    zx_status_t status = video_buffer_->FrameCompleted();
    // No longer have a frame offset to write to.
    has_video_buffer_offset_ = false;
    if (status != ZX_OK) {
      zxlogf(ERROR, "could not mark frame as complete: %d\n", status);
      return ZX_ERR_BAD_STATE;
    }

  } else {
    // No bytes were received, so don't send a notification.
    return ZX_OK;
  }

  zxlogf(SPEW,
         "sending NOTIFY_FRAME, timestamp = %ld, size: %u, offset: %lu, error "
         "= %d\n",
         event.metadata.timestamp, event.frame_size, event.frame_offset,
         event.frame_status);

  camera_control_->OnFrameAvailable(event);

  return ZX_OK;
}

zx_status_t UsbVideoStream::ParsePayloadHeaderLocked(
    usb_request_t* req, uint32_t* out_header_length) {
  // Different payload types have different header types but always share
  // the same first two bytes.
  usb_video_vs_payload_header header;
  size_t len = usb_req_copy_from(&usb_, req, &header,
                                 sizeof(usb_video_vs_payload_header), 0);

  if (len != sizeof(usb_video_vs_payload_header) ||
      header.bHeaderLength > req->response.actual) {
    zxlogf(ERROR, "got invalid header bHeaderLength %u data length %lu\n",
           header.bHeaderLength, req->response.actual);
    return ZX_ERR_INTERNAL;
  }

  uint8_t fid = header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_FID;
  // We can detect the start of a new frame via FID or EOF.
  //
  // FID is toggled when a new frame begins. This means any in progress frame
  // is now complete, and we are currently parsing the header of a new frame.
  //
  // If EOF was set on the previous frame, that means it was also completed,
  // and this is a new frame.
  bool new_frame = cur_frame_state_.fid != fid || cur_frame_state_.eof;
  if (new_frame) {
    // Notify the client of the completion of the previous frame.
    // We need to check if the currently stored FID is valid, and we didn't
    // already send a notification (EOF bit set).
    if (cur_frame_state_.fid >= 0 && !cur_frame_state_.eof) {
      zx_status_t status = FrameNotifyLocked();
      if (status != ZX_OK) {
        zxlogf(ERROR, "failed to send notification to client, err: %d\n",
               status);
        // Even if we failed to send a notification, we should
        // probably continue processing the new frame.
      }
    }

    // Initialize the frame state for the new frame.
    cur_frame_state_ = {};
    cur_frame_state_.fid = fid;
    num_frames_++;

    if (!has_video_buffer_offset_) {
      // Need to find a new frame offset to store the data in.
      zx_status_t status = video_buffer_->GetNewFrame(&video_buffer_offset_);
      if (status == ZX_OK) {
        has_video_buffer_offset_ = true;
      } else if (status == ZX_ERR_NOT_FOUND) {
        zxlogf(ERROR, "no available frames, dropping frame #%u\n", num_frames_);
      } else if (status != ZX_OK) {
        zxlogf(ERROR, "failed to get new frame, err: %d\n", status);
      }
    }
  }
  cur_frame_state_.eof = header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_EOF;

  if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_ERR) {
    // Only print the error message for the first erroneous payload of the
    // frame.
    if (!cur_frame_state_.error) {
      zxlogf(ERROR, "payload of frame #%u had an error bit set\n", num_frames_);
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
    // We need to update the total bytes counter before checking the error
    // field, otherwise we might return early and start of payload detection
    // will be wrong.
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
  if (!has_video_buffer_offset_) {
    // There was no space in the video buffer when the frame's first payload
    // header was parsed.
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

  uint8_t* dst =
      reinterpret_cast<uint8_t*>(video_buffer_->virt()) + frame_end_offset;
  usb_req_copy_from(&usb_, req, dst, data_size, header_len);

  cur_frame_state_.bytes += data_size;

  if (cur_frame_state_.eof) {
    // Send a notification to the client for frame completion now instead of
    // waiting to parse the next payload header, in case this is the very last
    // payload.
    zx_status_t status = FrameNotifyLocked();
    if (status != ZX_OK) {
      zxlogf(ERROR, "failed to send notification to client, err: %d\n", status);
    }
  }
}

void UsbVideoStream::DeactivateVideoBuffer() {
  fbl::AutoLock lock(&lock_);

  if (streaming_state_ != StreamingState::STOPPED) {
    streaming_state_ = StreamingState::STOPPING;
  }
}

void UsbVideoStream::DdkUnbind() {
  // Unpublish our device node.
  DdkRemove();
}

void UsbVideoStream::DdkRelease() { delete this; }

}  // namespace usb
}  // namespace video
