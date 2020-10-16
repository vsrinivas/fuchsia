// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_STREAM_H_
#define SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_STREAM_H_

#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fzl/vmo-pool.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/vector.h>
#include <usb/usb.h>

#include "src/camera/drivers/usb_video/camera_control_impl.h"
#include "src/camera/drivers/usb_video/usb_video.h"
#include "src/camera/drivers/usb_video/uvc_format.h"
#include "src/lib/listnode/listnode.h"

namespace video {
namespace usb {

static constexpr int USB_ENDPOINT_INVALID = -1;

class UsbVideoStream;
using UsbVideoStreamBase = ddk::Device<UsbVideoStream, ddk::Messageable, ddk::Unbindable>;

class UsbVideoStream : public UsbVideoStreamBase, public ddk::EmptyProtocol<ZX_PROTOCOL_CAMERA> {
 public:
  static zx_status_t Create(zx_device_t* device, usb_protocol_t* usb, int index,
                            usb_interface_descriptor_t* intf,
                            usb_video_vc_header_desc* control_header,
                            usb_video_vs_input_header_desc* input_header, UvcFormatList format_list,
                            fbl::Vector<UsbVideoStreamingSetting>* settings,
                            UsbDeviceInfo device_info, size_t parent_req_size);

  // DDK device implementation
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_camera_Device_dispatch(this, txn, msg, &CAMERA_FIDL_THUNKS);
  }
  ~UsbVideoStream();

 private:
  enum class StreamingState {
    STOPPED,
    STOPPING,
    STARTED,
  };

  // Device FIDL implementation
  zx_status_t GetChannel(zx_handle_t handle);

  UsbVideoStream(zx_device_t* parent, usb_protocol_t* usb, UvcFormatList format_list,
                 fbl::Vector<UsbVideoStreamingSetting>* settings, UsbDeviceInfo device_info,
                 size_t parent_req_size);

  zx_status_t Bind(const char* devname, usb_interface_descriptor_t* intf,
                   usb_video_vc_header_desc* control_header,
                   usb_video_vs_input_header_desc* input_header);

  // Requests the device use the given format and frame descriptor,
  // then finds a streaming setting that supports the required
  // data throughput.
  // If successful, sets the initial state of the stream configuration
  // related fields, and reallocates usb requests if necessary.
  // Otherwise an error will be returned and the caller should try
  // again with a different set of inputs.
  //
  // frame_desc may be NULL for non frame based formats.
  zx_status_t TryFormatLocked(uint8_t format_index, uint8_t frame_index,
                              uint32_t default_frame_interval) __TA_REQUIRES(lock_);

 public:
  // Interface with the FIDL Camera Driver
  zx_status_t GetFormats(std::vector<fuchsia::camera::VideoFormat>& formats);

  // Get the vendor and product information for this device.
  const UsbDeviceInfo& GetDeviceInfo() { return device_info_; }

  zx_status_t CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                           fuchsia::camera::FrameRate frame_rate);

  zx_status_t StartStreaming();

  zx_status_t StopStreaming();

  zx_status_t FrameRelease(uint64_t frame_offset);

  void DeactivateVideoBuffer();

 private:
  // Populates the free_reqs_ list with usb requests of the specified size.
  // Returns immediately if the list already contains large enough usb
  // requests, otherwise frees existing requests before allocating new ones.
  // The current streaming state must be StreamingState::STOPPED.
  zx_status_t AllocUsbRequestsLocked(uint64_t size) __TA_REQUIRES(lock_);
  // Queues a usb request against the underlying device.
  void QueueRequestLocked() __TA_REQUIRES(lock_);
  void RequestComplete(usb_request_t* req);

  static void RequestCompleteCallback(void* ctx, usb_request_t* request);
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
  zx_status_t ParsePayloadHeaderLocked(usb_request_t* req, uint32_t* out_header_length)
      __TA_REQUIRES(lock_);
  // Extracts the payload data from the usb request response,
  // and stores it in the video buffer.
  void ProcessPayloadLocked(usb_request_t* req) __TA_REQUIRES(lock_);

  usb_protocol_t usb_;
  UvcFormatList format_list_;

  fbl::Vector<UsbVideoStreamingSetting> streaming_settings_;

  // Stream configuration.
  usb_video_vc_probe_and_commit_controls negotiation_result_;
  const UsbVideoStreamingSetting* cur_streaming_setting_;
  uint32_t max_frame_size_ = 0;
  uint32_t clock_frequency_hz_ = 0;
  // The number of bytes to request in a USB request to a streaming endpoint.
  // This should be equal or less than allocated_req_size_.
  uint64_t send_req_size_ = 0;

  int streaming_ep_type_ = USB_ENDPOINT_INVALID;
  uint8_t iface_num_ = 0;
  uint8_t usb_ep_addr_ = 0;

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

  volatile StreamingState streaming_state_ __TA_GUARDED(lock_) = StreamingState::STOPPED;

  list_node_t free_reqs_ __TA_GUARDED(lock_);
  uint32_t num_free_reqs_ __TA_GUARDED(lock_);
  uint32_t num_allocated_reqs_ = 0;
  // Size of underlying VMO backing the USB request.
  uint64_t allocated_req_size_ = 0;

  // usb request size queried from the usb stack
  size_t parent_req_size_ = 0;
  // Bulk transfer specific fields.
  // Total bytes received so far for the current payload, including headers.
  // A bulk payload may be split across multiple usb requests,
  // whereas for isochronous it is always one payload per usb request.
  uint32_t bulk_payload_bytes_ = 0;

  fbl::Mutex lock_;

  // CameraStream FIDL interface
  std::unique_ptr<camera::ControlImpl> camera_control_ __TA_GUARDED(lock_) = nullptr;

  static const fuchsia_hardware_camera_Device_ops_t CAMERA_FIDL_THUNKS;
  // Loop used to run the FIDL server
  static std::unique_ptr<async::Loop> fidl_dispatch_loop_;

  fzl::VmoPool buffers_;
  fzl::VmoPool::Buffer current_buffer_;

  // The vendor and product information for this device.
  UsbDeviceInfo device_info_;
};

}  // namespace usb
}  // namespace video

#endif  // SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_STREAM_H_
