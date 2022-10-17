// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/usb_video/usb_state.h"

#include <lib/ddk/debug.h>
#include <lib/zircon-internal/align.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/lib/listnode/listnode.h"

namespace camera::usb_video {

static constexpr uint32_t MAX_OUTSTANDING_REQS = 8;

namespace {

// The biggest size we are allowing USB descriptor strings to be.
// Technically, the bLength field for strings is one byte, so the
// max size for any string should be 255.
static constexpr size_t kUsbDescriptorStringSize = 256;

// Extract strings from the device description.
// Strings in the usb device description are represented as indices
// into the 'strings' section at the end.  This function unpacks a specific
// string, given its index.
std::string FetchString(const usb_protocol_t& usb_proto, uint8_t description_index) {
  uint8_t str_buf[kUsbDescriptorStringSize];
  size_t buflen = sizeof(str_buf);
  uint16_t language_id = 0;
  zx_status_t res = usb_get_string_descriptor(&usb_proto, description_index, language_id,
                                              &language_id, str_buf, buflen, &buflen);
  if (res != ZX_OK) {
    return std::string();
  }

  buflen = std::min(buflen, sizeof(str_buf));
  return std::string(reinterpret_cast<char*>(str_buf), buflen);
}

void print_controls(const usb_video_vc_probe_and_commit_controls& proposal) {
  zxlogf(DEBUG, "bmHint 0x%x", proposal.bmHint);
  zxlogf(DEBUG, "bFormatIndex: %u", proposal.bFormatIndex);
  zxlogf(DEBUG, "bFrameIndex: %u", proposal.bFrameIndex);
  zxlogf(DEBUG, "dwFrameInterval: %u", proposal.dwFrameInterval);
  zxlogf(DEBUG, "dwMaxVideoFrameSize: %u", proposal.dwMaxVideoFrameSize);
  zxlogf(DEBUG, "dwMaxPayloadTransferSize: %u", proposal.dwMaxPayloadTransferSize);
}

zx::result<usb_video_vc_probe_and_commit_controls> ClearIfIoErrors(zx_status_t status,
                                                                   usb_protocol_t* usb) {
  if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
    usb_reset_endpoint(usb, 0);
  }
  zxlogf(ERROR, "usb_video_negotiate_probe failed: %d", status);
  return zx::error(status);
}

// TODO(fxbug.dev/104233): Use of dwMaxVideoFrameBufferSize for certain formats has been
// deprecated.  The dwMaxVideoFrameSize field obtained here should be used instead.
zx::result<usb_video_vc_probe_and_commit_controls> ProbeAndCommit(usb_protocol_t* usb,
                                                                  uint8_t iface_num,
                                                                  uint8_t format_index,
                                                                  uint8_t frame_index,
                                                                  uint32_t default_frame_interval) {
  usb_video_vc_probe_and_commit_controls proposal;
  memset(&proposal, 0, sizeof(usb_video_vc_probe_and_commit_controls));
  proposal.bmHint = USB_VIDEO_BM_HINT_FRAME_INTERVAL;
  proposal.bFormatIndex = format_index;

  proposal.bFrameIndex = frame_index;
  proposal.dwFrameInterval = default_frame_interval;

  zx_status_t status;

  zxlogf(DEBUG, "usb_video_negotiate_probe: PROBE_CONTROL SET_CUR");
  print_controls(proposal);
  // The wValue field (the fourth parameter) specifies the Control Selector
  // (in this case USB_VIDEO_VS_PROBE_CONTROL) in the high byte,
  // and the low byte must be set to zero.
  // See UVC 1.5 Spec. 4.2.1 Interface Control Requests.
  status = usb_control_out(usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_VIDEO_SET_CUR, USB_VIDEO_VS_PROBE_CONTROL << 8, iface_num,
                           ZX_TIME_INFINITE, (uint8_t*)&proposal, sizeof(proposal));
  if (status != ZX_OK) {
    return ClearIfIoErrors(status, usb);
  }

  // The length of returned result varies, so zero this out before hand.
  usb_video_vc_probe_and_commit_controls result;
  memset(&result, 0, sizeof(usb_video_vc_probe_and_commit_controls));

  zxlogf(DEBUG, "usb_video_negotiate_probe: PROBE_CONTROL GET_CUR");
  size_t out_length;
  status = usb_control_in(usb, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_VIDEO_GET_CUR,
                          USB_VIDEO_VS_PROBE_CONTROL << 8, iface_num, ZX_TIME_INFINITE,
                          (uint8_t*)&result, sizeof(result), &out_length);
  if (status != ZX_OK) {
    return ClearIfIoErrors(status, usb);
  }
  // Fields after dwMaxPayloadTransferSize are optional, only 26 bytes are
  // guaranteed.
  if (out_length < 26) {
    zxlogf(ERROR, "usb_video_negotiate_probe: got length %lu, want >= 26", out_length);
  } else {
    print_controls(result);
  }

  uint32_t dwMaxPayloadTransferSize = result.dwMaxPayloadTransferSize;

  status = usb_control_out(usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_VIDEO_SET_CUR, USB_VIDEO_VS_COMMIT_CONTROL << 8, iface_num,
                           ZX_TIME_INFINITE, (uint8_t*)&result, sizeof(result));
  if (status != ZX_OK) {
    if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
      // clear the stall/error
      usb_reset_endpoint(usb, 0);
    }
    zxlogf(ERROR, "usb_video_negotiate_commit failed: %d", status);
    return zx::error(status);
  }
  ZX_DEBUG_ASSERT(result.dwMaxPayloadTransferSize == dwMaxPayloadTransferSize);

  return zx::ok(result);
}

}  // namespace

// static
std::string UsbState::State2String(UsbState::StreamingState state) {
  switch (state) {
    case UsbState::StreamingState::STOPPED:
      return "STOPPED";
    case UsbState::StreamingState::READY:
      return "READY";
    case UsbState::StreamingState::STREAMING:
      return "STREAMING";
    case UsbState::StreamingState::STOPPING:
      return "STOPPING";
  }
  return "INVALID STATE";
}

//  static
zx::result<UsbRequest> UsbRequest::Create(usb_protocol_t* usb, uint8_t ep_addr, uint64_t size) {
  uint64_t parent_req_size = usb_get_request_size(usb);
  if (!parent_req_size) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  usb_request_t* req;
  auto status = usb_request_alloc(&req, size, ep_addr, parent_req_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_request_alloc failed: %d", status);
    return zx::error(status);
  }
  return zx::ok(UsbRequest(req));
}

UsbRequest::~UsbRequest() {
  if (req_) {
    usb_request_release(req_);
  }
}

const UsbDeviceInfo UsbState::GetDeviceInfo() const {
  usb_device_descriptor_t usb_dev_desc;
  UsbDeviceInfo device_info;
  // Fetch our top level device descriptor, so we know stuff like the values
  // of our VID/PID.
  usb_get_device_descriptor(&usb_, &usb_dev_desc);
  device_info.vendor_id = usb_dev_desc.id_vendor;
  device_info.product_id = usb_dev_desc.id_product;
  // Attempt to fetch the string descriptors for our manufacturer name,
  // product name, and serial number.
  if (usb_dev_desc.i_manufacturer) {
    device_info.manufacturer = FetchString(usb_, usb_dev_desc.i_manufacturer);
  }

  if (usb_dev_desc.i_product) {
    device_info.product_name = FetchString(usb_, usb_dev_desc.i_product);
  }

  if (usb_dev_desc.i_serial_number) {
    device_info.serial_number = FetchString(usb_, usb_dev_desc.i_serial_number);
  }
  return device_info;
}

UsbState::UsbState(usb_protocol_t usb, StreamingSetting settings)
    : usb_(usb),
      usb_ep_addr_(settings.input_header.bEndpointAddress),
      iface_num_(settings.vs_interface.b_interface_number),
      dev_clock_frequency_hz_(settings.hw_clock_frequency),
      streaming_settings_(std::move(settings)),
      req_complete_callback_({
          .callback =
              [](void* ctx, usb_request_t* request) {
                ZX_DEBUG_ASSERT(ctx != nullptr);
                reinterpret_cast<UsbState*>(ctx)->RequestComplete(request);
              },
          .ctx = this,
      }) {}

zx_status_t UsbState::StartStreaming(fit::function<void(usb_request_t*)> req_callback) {
  fbl::AutoLock lock(&lock_);
  // We should never receive commands while in the STOPPING state.
  // We consider it an error to call StartStreaming while already streaming.
  if (streaming_state_ != StreamingState::READY) {
    if (streaming_state_ == StreamingState::STOPPED) {
      zxlogf(ERROR, "SetFormat must be called before streaming can start.");
    } else {
      zxlogf(ERROR,
             "StartStreaming is only allowed while UsbState is in READY state. Current state: %s",
             State2String(streaming_state_).c_str());
    }
    return ZX_ERR_BAD_STATE;
  }
  zxlogf(DEBUG, "UsbVideoStream::usb_set_interface");
  zx_status_t status = usb_set_interface(&usb_, iface_num_, alt_setting_);
  if (status != ZX_OK) {
    return status;
  }
  req_callback_ = std::move(req_callback);
  ZX_DEBUG_ASSERT(free_requests_.size() == allocated_requests_.size());
  while (free_requests_.size()) {
    usb_request_queue(&usb_, free_requests_.back(), &req_complete_callback_);
    free_requests_.pop_back();
  }
  streaming_state_ = StreamingState::STREAMING;
  return ZX_OK;
}

zx_status_t UsbState::StopStreaming() {
  {
    fbl::AutoLock lock(&lock_);
    if (streaming_state_ != StreamingState::STREAMING) {
      if (streaming_state_ == StreamingState::STOPPING) {
        zxlogf(ERROR, "Detected temporary STOPPING state while calling StopStreaming.");
      } else {
        // streaming_state_ == STOPPED or READY
        zxlogf(ERROR, "Called StopStreaming, but stream was in state:%s.",
               State2String(streaming_state_).c_str());
      }
      return ZX_ERR_BAD_STATE;
    }
    // We can now assume that our current state is STREAMING.
    // Set the temporary state STOPPING.  This should prevent any other changes to state.
    streaming_state_ = StreamingState::STOPPING;
  }  // Release lock b/c usb_cancel_all calls all callbacks synchronously
  // Calling cancel all here to flush out all the requests we have queued.
  usb_cancel_all(&usb_, usb_ep_addr_);

  fbl::AutoLock lock(&lock_);
  ZX_ASSERT_MSG(streaming_state_ == StreamingState::STOPPING,
                "Error: The streaming state changed unexpectedly from STOPPING during "
                "StopStreaming()");
  zx_status_t status = ZX_OK;
  if (free_requests_.size() != allocated_requests_.size()) {
    zxlogf(ERROR, "Canceling the stream did not drain all the remaining requests.");
    // Not much to do at this point.  Perhaps reset the usb in a more drastic manner?
    // At the very least, return an error to indicate something went wrong.
    status = ZX_ERR_INTERNAL;
  } else {
    zxlogf(DEBUG, "setting video buffer as stopped");
  }

  streaming_state_ = StreamingState::READY;
  usb_set_interface(&usb_, iface_num_, 0);
  return status;
}

UsbState::~UsbState() {}

zx_status_t UsbState::Allocate(uint64_t size, int ep_type) {
  if (streaming_state_ != StreamingState::STOPPED && streaming_state_ != StreamingState::READY) {
    zxlogf(ERROR, "Failed to allocate usb requests because state != STOPPED or READY");
    return ZX_ERR_BAD_STATE;
  }
  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  send_req_size_ = size;
  if (size <= allocated_req_size_) {
    zxlogf(DEBUG, "reusing allocated usb requests. had %lu, needed %lu", allocated_req_size_, size);
    // Can reuse existing usb requests.  Update the length field to the current size:
    for (UsbRequest& req : allocated_requests_) {
      req.req()->header.length = send_req_size_;
    }

    return ZX_OK;
  }
  // Need to allocate new usb requests, release any existing ones.

  ZX_DEBUG_ASSERT(free_requests_.size() == allocated_requests_.size());
  allocated_requests_.clear();
  free_requests_.clear();
  // If we are doing isochronous streaming, we can prevent multiple re-allocations by just
  // allocating to the maximum request size.  This won't usually cause unnecessary memory
  // allocation because the requests are actually backed by vmos that are rounded up to
  // the page size.  Also this is what the previous version of the driver did...

  // So, if this is the first time allocating requests for an isochronous stream, allocate
  // the maximum bandwidth instead.
  if ((ep_type == USB_ENDPOINT_ISOCHRONOUS) && (allocated_req_size_ == 0)) {
    for (const auto& setting : streaming_settings_.endpoint_settings) {
      if (setting.isoc_bandwidth > size) {
        size = setting.isoc_bandwidth;
      }
    }
  }

  zxlogf(DEBUG, "allocating %d usb requests of size %lu", MAX_OUTSTANDING_REQS, size);

  for (uint32_t i = 0; i < MAX_OUTSTANDING_REQS; i++) {
    zx::result<UsbRequest> req_or = UsbRequest::Create(&usb_, usb_ep_addr_, size);
    if (req_or.is_error()) {
      zxlogf(ERROR, "usb_request_alloc failed: %d", req_or.error_value());
      allocated_requests_.clear();
      return req_or.error_value();
    }
    allocated_requests_.push_back(std::move(*req_or));
    free_requests_.push_back(allocated_requests_.back().req());
    zxlogf(DEBUG, "adding allocated requests... %lu ", allocated_requests_.size());
  }
  allocated_req_size_ = size;
  return ZX_OK;
}

zx_status_t UsbState::SetFormat(fuchsia::camera::VideoFormat video_format) {
  fbl::AutoLock lock(&lock_);
  // We should never receive commands while in the STOPPING state.
  if (streaming_state_ == StreamingState::STOPPING) {
    zxlogf(ERROR, "SetFormat called while while UsbState is in temporary Stopping state.");
    return ZX_ERR_BAD_STATE;
  }
  // We do not allow changing formats while streaming.
  if (streaming_state_ == StreamingState::STREAMING) {
    zxlogf(ERROR, "cannot set video format while streaming is not stopped");
    return ZX_ERR_BAD_STATE;
  }
  // Convert from the client's video format proto to the device driver format
  // and frame descriptors.
  auto match = std::find_if(streaming_settings_.formats.begin(), streaming_settings_.formats.end(),
                            [video_format](const UvcFormat& f) { return f == video_format; });
  if (match == streaming_settings_.formats.end()) {
    zxlogf(ERROR, "could not find a mapping for the requested format");
    return ZX_ERR_NOT_FOUND;
  }

  // Now push the settings to the usb device:
  zxlogf(DEBUG, "trying format %u, frame desc %u", match->format_index, match->frame_index);
  auto negotiation_result = ProbeAndCommit(&usb_, iface_num_, match->format_index,
                                           match->frame_index, match->default_frame_interval);
  if (negotiation_result.is_error()) {
    return negotiation_result.error_value();
  }

  // Now, find a stream setting that will support the config we just committed.
  // TODO(jocelyndang): we should calculate this ourselves instead
  // of reading the reported value, as it is incorrect in some devices.
  uint32_t required_bandwidth = negotiation_result->dwMaxPayloadTransferSize;

  const StreamingEndpointSetting* best_setting = nullptr;
  // Find a setting that supports the required bandwidth.
  for (const auto& setting : streaming_settings_.endpoint_settings) {
    // For bulk transfers, we use the first (and only) setting.
    if (setting.ep_type == USB_ENDPOINT_BULK || setting.isoc_bandwidth >= required_bandwidth) {
      best_setting = &setting;
      break;
    }
  }
  if (!best_setting) {
    zxlogf(ERROR, "could not find a setting with bandwidth >= %u", required_bandwidth);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint64_t allocate_req_size = best_setting->isoc_bandwidth;
  if (best_setting->ep_type == USB_ENDPOINT_BULK) {
    allocate_req_size =
        std::min(usb_get_max_transfer_size(&usb_, usb_ep_addr_),
                 static_cast<uint64_t>(negotiation_result->dwMaxPayloadTransferSize));
  }
  auto status = Allocate(allocate_req_size, best_setting->ep_type);
  if (status == ZX_OK) {
    // We're now configured for a specific setting, save that:
    alt_setting_ = best_setting->alt_setting;
    negotiation_result_ = *negotiation_result;
    ep_type_ = best_setting->ep_type;

    // dwClockFrequency is an optional field of the negotiation.
    // Use it if present.
    if (negotiation_result->dwClockFrequency != 0) {
      dev_clock_frequency_hz_ = negotiation_result->dwClockFrequency;
    }
    streaming_state_ = StreamingState::READY;
  }
  return status;
}

// when we get a request, send it to the callback and put it back in the queue:
void UsbState::RequestComplete(usb_request_t* req) {
  fbl::AutoLock lock(&lock_);
  // Check if we are in the stopping state.  If so, do not send updates or re-queue
  // the requests.
  if (streaming_state_ == StreamingState::STOPPING) {
    zxlogf(DEBUG, "RequestComplete: status: %s", zx_status_get_string(req->response.status));
    free_requests_.push_back(req);
    zxlogf(DEBUG, "Draining requests: %lu / %lu", free_requests_.size(),
           allocated_requests_.size());
  } else {
    // The only other legal state is to be currently streaming.
    ZX_ASSERT_MSG(streaming_state_ == StreamingState::STREAMING,
                  "Received request callback, but stream was not running! "
                  "Something is wrong with the state machine!!");
    // We call the callback while holding the lock. We therefore place the restriction that
    // none of our methods can be called from within this callback.
    req_callback_(req);
    usb_request_queue(&usb_, req, &req_complete_callback_);
  }
}

}  // namespace camera::usb_video
