// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/usb_video/video_frame.h"

#include <lib/ddk/debug.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-pool.h>

#include <memory>

#include <usb/usb-request.h>
#include <usb/usb.h>

namespace camera::usb_video {

// Only keep the first 11 bits of the USB SOF (Start of Frame) values.
// The payload header SOF values only have 11 bits before wrapping around,
// whereas the XHCI host returns 64 bits.
static constexpr uint16_t USB_SOF_MASK = 0x7FF;

zx::result<fuchsia::camera::FrameAvailableEvent> VideoFrame::Release() {
  if (bytes_ == 0) {
    zxlogf(WARNING, "VideoFrame::Release: bytes == 0");
    return zx::error(ZX_ERR_BAD_STATE);
  }
  fuchsia::camera::FrameAvailableEvent event;
  event.metadata.timestamp = capture_time_;

  if (!buffer_.valid()) {
    event.frame_status = fuchsia::camera::FrameStatus::ERROR_BUFFER_FULL;
    zxlogf(WARNING, "VideoFrame::Release: !buffer_.valid()");
    return zx::ok(event);
  }
  event.buffer_id = buffer_.ReleaseWriteLockAndGetIndex();
  zxlogf(DEBUG, "buffer_.ReleaseWriteLockAndGetIndex got %u", event.buffer_id);
  // If we were writing to a buffer (we have to complete it)
  // If we had a writing error:
  if (error_) {
    event.frame_status = fuchsia::camera::FrameStatus::ERROR_FRAME;
  }
  return zx::ok(event);
}

// static
zx::result<VideoFrame::PayloadHeader> VideoFrame::PayloadHeader::ParseHeader(usb_request_t* req) {
  if (req->response.status != ZX_OK) {
    failure_count_++;
    if (failure_count_ == 1 || (failure_count_) % 10 == 0) {
      zxlogf(ERROR, "usb request failed %lu times: %d", failure_count_.load(),
             req->response.status);
    }
    return zx::error(req->response.status);
  }
  usb_video_vs_payload_header pheader;
  size_t len = usb_request_copy_from(req, &pheader, sizeof(pheader), 0);
  if (len != sizeof(usb_video_vs_payload_header) || pheader.bHeaderLength > req->response.actual) {
    zxlogf(ERROR, "got invalid header bHeaderLength %u data length %lu", pheader.bHeaderLength,
           req->response.actual);
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(PayloadHeader{.header = pheader});
}

bool VideoFrame::IsPayloadComplete(usb_request_t* req) {
  if (usb_state_.EndpointType() == USB_ENDPOINT_ISOCHRONOUS) {
    return true;
  }
  bulk_payload_bytes_ += req->response.actual;
  // A payload is complete when we've received enough bytes to reach the max
  // payload size, or fewer bytes than what we requested.
  if (bulk_payload_bytes_ >= usb_state_.MaxPayloadTransferSize() ||
      req->response.actual < usb_state_.RequestSize()) {
    bulk_payload_bytes_ = 0;
    return true;
  }
  return false;
}

// Extracts the payload data from the usb request response,
// and stores it in the video buffer.
zx_status_t VideoFrame::ProcessPayload(usb_request_t* req) {
  // Empty responses should be ignored.
  if (req->response.actual == 0 || req->response.status != ZX_OK) {
    return ZX_OK;
  }
  uint32_t header_offset = 0;
  // Read the header for the first request
  if (HasPayloadHeader()) {
    auto header_result = PayloadHeader::ParseHeader(req);
    if (header_result.is_error()) {
      zxlogf(ERROR, "payload header has error");
      return ZX_ERR_INTERNAL;
    }
    if (header_result->error()) {
      // Only print the error message for the first erroneous payload of the
      // frame.
      if (!error_) {
        zxlogf(ERROR, "payload of frame had an error bit set");
        error_ = true;
      }
    }

    eof_ = header_result->eof();
    // Check if this should be a new frame.  This should not happen if
    // we are detecting the eof flag correctly.
    // if fid == -1, that just means this is the first payload of the frame.
    if (fid_ != -1 && fid_ != header_result->fid()) {
      zxlogf(DEBUG, "new frame detected");
      return ZX_ERR_NEXT;
    }
    fid_ = header_result->fid();  // set fid, if it is the first payload.
    header_offset = header_result->length();
    ParseHeaderTimestamps(req);
  }
  zx_status_t status = ZX_ERR_INTERNAL;
  if (!error_) {
    status = CopyData(req, header_offset);
    if (status != ZX_OK) {
      error_ = true;  // Don't return yet, we may have just finished a frame.
    }
  }
  if (IsPayloadComplete(req) && eof_ == true) {
    return ZX_ERR_STOP;
  }
  return status;
}

zx_status_t VideoFrame::CopyData(usb_request_t* req, uint32_t data_offset) {
  if (!buffer_.valid()) {
    // There was no space in the video buffer when the frame's first payload
    // header was parsed.
    return ZX_ERR_BAD_STATE;
  }

  // Copy the data into the video buffer.
  uint32_t data_size = static_cast<uint32_t>(req->response.actual) - data_offset;
  if (bytes_ + data_size > usb_state_.MaxFrameSize()) {
    zxlogf(ERROR, "invalid data size %u, cur frame bytes %u, frame size %u", data_size, bytes_,
           usb_state_.MaxFrameSize());
    error_ = true;
    return ZX_ERR_IO_INVALID;
  }

  // Append the data to the end of the current frame.
  uint64_t avail = buffer_.size() - bytes_;
  ZX_DEBUG_ASSERT(avail >= data_size);

  uint8_t* dst = reinterpret_cast<uint8_t*>(buffer_.virtual_address()) + bytes_;
  // TODO (fxb/63635): Decide what to do here
  __UNUSED auto copy_result = usb_request_copy_from(req, dst, data_size, data_offset);

  bytes_ += data_size;
  return ZX_OK;
}

static inline double device_clock_to_ms(uint32_t clock_reading, uint32_t clock_frequency_hz) {
  return clock_frequency_hz != 0 ? clock_reading * 1000.0 / clock_frequency_hz : 0;
}

void VideoFrame::ParseHeaderTimestamps(usb_request_t* req) {
  // TODO(jocelyndang): handle other formats, the timestamp offset is variable.
  usb_video_vs_uncompressed_payload_header header = {};
  __UNUSED auto result =
      usb_request_copy_from(req, &header, sizeof(usb_video_vs_uncompressed_payload_header), 0);

  // PTS should stay the same for payloads of the same frame,
  // but it's probably not a critical error if they're different.
  if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_PTS) {
    uint32_t new_pts = header.dwPresentationTime;

    // Use the first seen PTS value.
    if (pts_ == 0) {
      pts_ = new_pts;
    } else if (new_pts != pts_) {
      zxlogf(ERROR, "#%u: PTS changed between payloads, from %u to %u", frame_number_, pts_,
             new_pts);
    }
  }

  if (header.bmHeaderInfo & USB_VIDEO_VS_PAYLOAD_HEADER_SCR) {
    uint32_t new_stc = header.scrSourceTimeClock;
    uint16_t new_sof = header.scrSourceClockSOFCounter;

    // The USB Video Class Spec 1.1 suggests that updated SCR values may
    // be provided per payload of a frame. Only use the first seen value.
    if (stc_ == 0) {
      stc_ = new_stc;
      device_sof_ = new_sof;
    }
  }

  // The device might not support header timestamps.
  if (pts_ == 0 || stc_ == 0) {
    return;
  }
  // Already calculated the capture time for the frame.
  if (capture_time_ != 0) {
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
  // TODO(fxbug.dev/104503): Use the IRQ timestamp instead of the system timestamp
  // for more accuracy.
  zx_time_t host_complete_time_ns = zx_clock_get_monotonic();

  // Calculate the difference between when raw frame capture starts and ends.
  uint32_t device_delay = stc_ - pts_;
  double device_delay_ms = device_clock_to_ms(device_delay, usb_state_.ClockFrequencyHz());

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
  uint16_t transport_delay_ms = (host_sof_ - device_sof_) & USB_SOF_MASK;

  // Time between when raw frame capture starts and the driver receiving the
  // frame.
  double total_video_delay = device_delay_ms + transport_delay_ms;

  // Start of raw frame capture as zx_time_t (nanoseconds).
  zx_time_t capture_start_ns = host_complete_time_ns - ZX_MSEC(total_video_delay);
  // The capture time is specified in the camera interface as the midpoint of
  // the capture operation, not including USB transport time.
  capture_time_ = capture_start_ns + ZX_MSEC(device_delay_ms) / 2;
}

}  // namespace camera::usb_video
