// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_USB_VIDEO_USB_STATE_H_
#define SRC_CAMERA_DRIVERS_USB_VIDEO_USB_STATE_H_

#include <lib/fit/function.h>

#include <memory>
#include <optional>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/camera/drivers/usb_video/descriptors.h"

namespace camera::usb_video {

// Information about the vendor and product that can be gleaned from the USB
// descriptor.
struct UsbDeviceInfo {
  uint16_t vendor_id;
  uint16_t product_id;
  std::string manufacturer;
  std::string product_name;
  std::string serial_number;
};

// RAII wrapper around the usb_request_t
class UsbRequest {
 public:
  static zx::result<UsbRequest> Create(usb_protocol_t* usb, uint8_t ep_addr, uint64_t size);
  UsbRequest(const UsbRequest& u) = delete;
  UsbRequest(UsbRequest&& u) : req_(u.req_) { u.req_ = nullptr; }
  ~UsbRequest();

  usb_request_t* req() { return req_; }

 private:
  explicit UsbRequest(usb_request_t* req) : req_(req) {}
  usb_request_t* req_;
};

// UsbState handles the state machine for interacting with the usb camera.
// It handles the validation and communication with the USB device
// when selecting an available stream, and starting and stopping that stream.
// UsbState communicates with the device through a banjo interface to parent usb device.
// Since the parent device may operate on a separate dispatcher, UsbState may exhibit one
// asynchronous behavior: the request callback passed as an argument to StartStreaming may
// be called using a different dispatcher than the one that called StartStreaming.
// All other functions of UsbState operate synchronously.
//
// The SetFormat function must be called successfully before a stream may be started or stopped.
// In this case, a "stream" is defined as a series of calls to the request callback provided
// by the StartStreaming function. Each call contains a usb_request_t, which points at a vmo with a
// blob of data.  Concatenated together, these data blobs make up the data portion of a single
// video frame.  The usb_header included in the usb_request_t contains information to indicate which
// video frame is being filled.
// State machine for UsbState:
//  STOPPED --- <SetFormat> ---> READY   <------
//                                 |         STOPPING (temporary)
//                        <StartStreaming>      |
//                                |         <StopStreaming>
//                               \|/            |
//                             STREAMING -------^
//
// Here are the allowed state transitions.
// Cells marked with ERROR will return an error if attempted, but
// will not result in a state transition.
//                    Initial State
//               | STOPPED |   READY   | STREAMING | STOPPING
// SetFormat     |  READY  |   READY   |   Error!  |  Error!
// StartStreaming|  Error! | STREAMING |   Error!  |  Error!
// StopStreaming |  Error! |   Error!  | STOPPING  |  Error!
//
// The STOPPING state is a temporary state that transitions to
// READY when stopping is complete.
class UsbState {
 public:
  UsbState(usb_protocol_t usb, StreamingSetting settings);

  // Requests the device use the given format and frame descriptor,
  // then finds a streaming setting that supports the required
  // data throughput.
  // If successful, sets the initial state of the stream configuration
  // related fields, and reallocates usb requests if necessary.
  // Otherwise an error will be returned and the caller should try
  // again with a different set of inputs.
  //
  // frame_desc may be NULL for non frame based formats.
  // Return: on success, returns the configuration that has been set.
  zx_status_t SetFormat(fuchsia::camera::VideoFormat video_format) __TA_EXCLUDES(lock_);

  zx_status_t StartStreaming(fit::function<void(usb_request_t*)> req_callback) __TA_EXCLUDES(lock_);

  zx_status_t StopStreaming() __TA_EXCLUDES(lock_);

  const UsbDeviceInfo GetDeviceInfo() const;

  const std::vector<UvcFormat>& GetUvcFormats() const { return streaming_settings_.formats; }

  uint32_t ClockFrequencyHz() const { return dev_clock_frequency_hz_; }

  // These functions can only be called when a format has been set:
  uint32_t MaxFrameSize() const __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    ZX_ASSERT(streaming_state_ != StreamingState::STOPPED);
    return negotiation_result_->dwMaxVideoFrameSize;
  }

  // The number of bytes to request in a USB request to a streaming endpoint.
  // This should be equal or less than allocated_req_size_.
  // Can only be called after format has been set
  uint64_t RequestSize() const __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    ZX_ASSERT(streaming_state_ != StreamingState::STOPPED);
    return send_req_size_;
  }

  // Can only be called after format has been set
  uint32_t MaxPayloadTransferSize() const __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    ZX_ASSERT(streaming_state_ != StreamingState::STOPPED);
    return negotiation_result_->dwMaxPayloadTransferSize;
  }

  // Can only be called after format has been set
  uint32_t EndpointType() const __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    ZX_ASSERT(streaming_state_ != StreamingState::STOPPED);
    return ep_type_;
  }

  uint64_t GetHostSOF() const { return usb_get_current_frame(&usb_); }

  ~UsbState();

 private:
  // Populates the free_reqs_ list with usb requests of the specified size.
  // Returns immediately if the list already contains large enough usb
  // requests, otherwise frees existing requests before allocating new ones.
  // The current streaming state must be StreamingState::STOPPED.
  zx_status_t Allocate(uint64_t size, int ep_type) __TA_REQUIRES(lock_);

  // when we get a request, send it to the callback and put it back in the queue:
  void RequestComplete(usb_request_t* req) __TA_EXCLUDES(lock_);

  enum class StreamingState {
    STOPPED,
    READY,
    STREAMING,
    STOPPING,
  };
  static std::string State2String(UsbState::StreamingState state);

  // Threading concerns:
  // There are two dispatchers that alter the state of this class.
  // 1) The single-threaded fidl dispatcher started by UsbVideoStream.
  //    This dispatcher may call all the public methods, three of which are not const:
  //    SetFormat, StartStreaming, StopStreaming
  //    Each of these changes the streaming_state_ and the request list.
  // 2) The banjo interface to the usb stack.
  //    This dispatcher may call RequestComplete, which changes the request list, and
  //    accesses the streaming_state_
  //
  // Therefore, to prevent concurrent access, the lock_ mutex guards the request list and
  // streaming_state_
  mutable fbl::Mutex lock_;
  usb_protocol_t usb_;
  uint8_t usb_ep_addr_ = 0;
  uint8_t iface_num_ = 0;

  // These variables are set by SetFormat().
  std::optional<usb_video_vc_probe_and_commit_controls> negotiation_result_ = std::nullopt;
  uint8_t alt_setting_ = 0;
  int ep_type_ = 0;

  // The frequency given by the control header:
  uint32_t dev_clock_frequency_hz_ = 0;
  StreamingSetting streaming_settings_;
  StreamingState streaming_state_ __TA_GUARDED(lock_) = StreamingState::STOPPED;

  // Size of underlying VMO backing the USB request.
  uint64_t allocated_req_size_ = 0;
  uint64_t send_req_size_ = 0;
  const usb_request_complete_callback_t req_complete_callback_;

  fit::function<void(usb_request_t*)> req_callback_ __TA_GUARDED(lock_);
  std::vector<usb_request_t*> free_requests_ __TA_GUARDED(lock_);
  std::vector<UsbRequest> allocated_requests_ __TA_GUARDED(lock_);
};

}  // namespace camera::usb_video

#endif  // SRC_CAMERA_DRIVERS_USB_VIDEO_USB_STATE_H_
