// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_USB_VIDEO_VIDEO_FRAME_H_
#define SRC_CAMERA_DRIVERS_USB_VIDEO_VIDEO_FRAME_H_

#include <lib/fit/function.h>
#include <lib/fzl/vmo-pool.h>

#include <memory>

#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/camera/drivers/usb_video/usb_state.h"

namespace camera::usb_video {

// VideoFrame represents one frame of a video stream.  Instances of this class
// are created when a new frame is detected, and destroyed when the frame is fully
// received into the buffer.
class VideoFrame {
 public:
  // Both the UsbState and the VmoPool which produced the buffer passed to VideoFrame
  // must be valid for the lifetime of the frame.
  VideoFrame(fzl::VmoPool::Buffer buffer, const UsbState& usb_state, uint32_t frame_number)
      : buffer_(std::move(buffer)), usb_state_(usb_state), frame_number_(frame_number) {
    host_sof_ = usb_state_.GetHostSOF();
  }

  // Releases the write lock on the buffer, and returns information
  // for transmitting that a frame is available for consumption.
  // This should only be called when ProcessPayload indicates
  // the frame is complete.
  // The returned FrameAvailableEvent accounts for frame errors, or if we didn't have a buffer.
  // Release will only return an error if there was a logical issue with the frame,
  // in which case the frame should not be sent to the stream client.
  zx::result<fuchsia::camera::FrameAvailableEvent> Release();

  // Extracts the payload data from the usb request response,
  // and stores it in the video buffer.
  // Returns:
  //   ZX_ERR_STOP - If the video frame has been completed by
  //                 this request.  Release() can now be called.
  //   ZX_ERR_NEXT - If the video frame has already been completed.
  //                 A new video frame should be created, and the
  //                 usb_request given should be passed to the new
  //                 frame.
  //   ZX_OK       - The data in the request was added to the frame.
  //   Other Errors- There is an error with the request or the frame.
  //                 No action needs to be taken. Although this frame
  //                 will have errors, it is still necessary to wait until
  //                 a new frame is started. The error flag will be set
  //                 for this frame when Release is called.
  zx_status_t ProcessPayload(usb_request_t* req);

  uint32_t FrameNumber() const { return frame_number_; }

 private:
  struct PayloadHeader {
    static zx::result<PayloadHeader> ParseHeader(usb_request_t* req);

    uint32_t length() const { return header.bHeaderLength; }
    uint8_t fid() const { return header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_FID; }
    uint8_t eof() const { return header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_EOF; }
    uint8_t error() const { return header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_ERR; }

    const usb_video_vs_payload_header header;
  };

  bool HasPayloadHeader() const {
    return usb_state_.EndpointType() != USB_ENDPOINT_BULK || bulk_payload_bytes_ == 0;
  }

  bool IsPayloadComplete(usb_request_t* req);

  zx_status_t CopyData(usb_request_t* req, uint32_t data_offset);

  void ParseHeaderTimestamps(usb_request_t* req);

  fzl::VmoPool::Buffer buffer_;
  // Reference to the usb state. Assumed to be valid for the lifetime of the frame.
  const UsbState& usb_state_;
  // Used for log messages.
  uint32_t frame_number_;

  // Bytes received so far for the frame.
  uint32_t bytes_ = 0;

  // FID is a bit that is toggled when a new frame begins,
  // and stays constant for the rest of the frame.
  // Some devices will not set this bit.
  int8_t fid_ = -1;
  // Whether we've received the last payload for the frame.
  // Some devices will not set this bit.
  bool eof_ = false;

  // Whether the frame contains an error.
  bool error_ = false;

  // Timekeeping:
  // Presentation timestamp for the frame. This is when the device
  // begins raw frame capture.
  uint32_t pts_ = 0;
  // Source time clock value for when the first video data of a
  // video frame is put on the USB bus.
  uint32_t stc_;
  // The USB frame number at the time that STC was sampled.
  // The largest value can have 11 bits set before wrapping around to zero.
  uint16_t device_sof_;

  // This is the 64 bit incremental frame number at the time the first
  // payload was received by the USB video driver.
  // The XHCI host handles the SOF value wrapping around, so this is 64 bits.
  uint64_t host_sof_;

  // The time at the midpoint of the capture operation, with respect
  // to the monotonic clock.
  zx_time_t capture_time_;

  // Bulk transfer specific fields.
  // Total bytes received so far for the current payload, including headers.
  // A bulk payload may be split across multiple usb requests,
  // whereas for isochronous it is always one payload per usb request.
  uint64_t bulk_payload_bytes_ = 0;

  // Counts the number of bad requests we have seen, for logging purposes.
  static inline std::atomic<uint64_t> failure_count_ = 0;
};

}  // namespace camera::usb_video

#endif  // SRC_CAMERA_DRIVERS_USB_VIDEO_VIDEO_FRAME_H_
