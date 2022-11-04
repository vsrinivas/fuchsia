// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_STREAM_H_
#define SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_STREAM_H_

#include <fidl/fuchsia.hardware.camera/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-pool.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/mutex.h>
#include <usb/usb.h>

#include "src/camera/drivers/usb_video/usb_state.h"
#include "src/camera/drivers/usb_video/video_frame.h"

namespace camera::usb_video {

class UsbVideoStream;
// Stores a usb_request_t in memory.
// An async_task is used here instead of a asyc::TaskMethod to that the task can
// keep track of the request to be handled.
class RequestTask : public async_task {
 public:
  RequestTask(usb_request_t* req, UsbVideoStream* context);
  static void AsyncTaskHandler(async_dispatcher_t* dispatcher, async_task_t* task,
                               zx_status_t status);
  usb_request_t* req() { return &req_; }

 private:
  usb_request_t req_;
  UsbVideoStream* context_;
  std::vector<uint8_t> data_;
};

// UsbVideoStream State Machine:
//
// [DISCONNECTED] ------<GetChannel()>---------> [STOPPED]
//          /|\                                 /       |
//           |          --------<stream.OnClose()>      |
//  <control.OnClose>--/--------------    /             |
//           |        /               \   |            \|/
//         [STREAMING] <--Start()--- [READY] <--- <CreateStream()>
//
//  All states ------> Unbind() ----> [UNBOUND]
//
// State transition table:
// Cells marked with Error will return an error if attempted, but
// will not result in a state transition.
// Cells marked with N/A are not possible due to lack of channel to
// make the call.
//                    Initial State
//               | DISCONNECTED |    STOPPED  |    READY     | STREAMING
// GetChannel    |   STOPPED    |     Error!  |    Error!    |  Error!
// CreateStream  |     N/A      |     READY   |    Error!    |  Error!
// Start         |     N/A      |      N/A    |  STREAMING   |  Error!
// Stop          |     N/A      |      N/A    |    Error!    |  READY
// stream close  |     N/A      |      N/A    |    STOPPED   |  STOPPED
// control close |     N/A      | DISCONNECTED| DISCONNECTED | DISCONNECTED
// DdkUnbind     |  UNBOUND     |   UNBOUND   |   UNBOUND    |   UNBOUND
//
// The Unbind action closes all channels, and disallows any further state transitions.
//
// The state can be determined as follows:
// if (unbind txn set)    -> UNBOUND
// if (!control channel)  -> DISCONNECTED
// elif (!stream channel) -> STOPPED
// elif (!is_streaming_)   -> READY
// else                    -> STREAMING
//
// This device does not allow multiple simultaneous control connections via GetChannel,
// nor does it allow multiple streaming connections via CreateStream.
// Any failure in communication over the control or stream channel will result in that channel
// being closed, along with the state transitions that closure implies.
//
// Calling Stop() may not stop the usb device from streaming video frames, but
// It will cause this device to stop notifying the client of new frames via OnFrameAvailable.
// When Start() is called after calling Stop(), OnFrameAvailable will be called when the next
// available frame is filled, so the capture time may have occurred before Start() was called.
//
// The stream_token eventpair passed with CreateStream will be ignored by this device,
// although it will retain ownership of the token until the stream is shut down following
// closure of the stream channel.
//
// Unless specified otherwise, all methods of this class are assumed to be called
// on the single threaded dispatcher owned by this class.
class UsbVideoStream;
using UsbVideoStreamBase =
    ::ddk::Device<UsbVideoStream, ::ddk::Unbindable,
                  ::ddk::Messageable<fuchsia_hardware_camera::Device>::Mixin>;

class UsbVideoStream : public UsbVideoStreamBase,
                       public fuchsia::camera::Control,
                       public fuchsia::camera::Stream,
                       public ::ddk::EmptyProtocol<ZX_PROTOCOL_CAMERA> {
 public:
  // Constructor is assumed to be called only through Bind, or suitable testing rig.
  // This method will not be called on the fidl_dispatch_loop_.
  UsbVideoStream(zx_device_t* parent, usb_protocol_t usb, StreamingSetting settings);

  // DDK device implementation
  // DdkUnbind posts an async task on the fidl_dispatch_loop_.
  void DdkUnbind(::ddk::UnbindTxn txn) __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    unbind_txn_ = std::move(txn);
    unbind_task_.Post(fidl_dispatch_loop_->dispatcher());
  }
  // Release is guaranteed to only be called after receiving the async reply
  // to DdkUnbind.
  // This method will not be called on the fidl_dispatch_loop_.
  void DdkRelease() { delete this; }
  ~UsbVideoStream() = default;

  // This method will not be called on the fidl_dispatch_loop_.
  static zx_status_t Bind(void* ctx, zx_device_t* device);

 private:
  // Fuchsia Hardware Camera FIDL implementation.
  // These three calls may not be called on the fidl_dispatch_loop_.
  void GetChannel2(GetChannel2RequestView request, GetChannel2Completer::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  // Call not supported
  void GetChannel(GetChannelRequestView request, GetChannelCompleter::Sync& completer) override
      __TA_EXCLUDES(lock_);
  void GetDebugChannel(GetDebugChannelRequestView request,
                       GetDebugChannelCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // --------------------------------------------------------------
  //     Fidl Implementation for fuchsia::camera::Control
  // --------------------------------------------------------------
  // Get the available format types for this device
  void GetFormats(uint32_t index, GetFormatsCallback callback) override;

  // Get the vendor and product information for this device
  void GetDeviceInfo(GetDeviceInfoCallback callback) override;

  // Configures a video stream.
  // The BufferCollectionInfo contains the desired image format as well as the
  // vmo-backed buffers that are used to transfer the video frames.
  // The |stream_token| will be closed by the driver when the stream is fully
  // stopped after the stream channel is closed.  Otherwise, it is ignored.
  void CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                    fuchsia::camera::FrameRate frame_rate,
                    fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                    zx::eventpair stream_token) override __TA_EXCLUDES(lock_);

  // --------------------------------------------------------------
  //     Fidl Implementation for fuchsia::camera::Stream
  // --------------------------------------------------------------

  // Starts the streaming of frames.
  void Start() override { CloseStreamOnError(SetStreaming(true), "start"); }

  // Stops the streaming of frames.
  void Stop() override { CloseStreamOnError(SetStreaming(false), "stop"); }

  // Unlocks the specified frame, allowing the driver to reuse the memory.
  void ReleaseFrame(uint32_t buffer_index) override {
    CloseStreamOnError(FrameRelease(buffer_index), "release frame");
  }

  // Sent by the driver to the client when a frame is available for
  // processing, or an error occurred.
  void OnFrameAvailable(const fuchsia::camera::FrameAvailableEvent& frame) {
    stream_binding_.events().OnFrameAvailable(frame);
  }

  // --------------------------------------------------------------
  //    Internal functions for interacting with streams
  // --------------------------------------------------------------
  // Finds a video configuration that matches the format of the buffer collection,
  // then maps the buffers given.  If a different configuration was previously used,
  // it is discarded.
  zx_status_t CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                           fuchsia::camera::FrameRate frame_rate);

  // Starts and stops the stream.  Retains the configuration when stopping so
  // the stream can be re-started.
  zx_status_t SetStreaming(bool stream);

  // Called when the stream channel is closed.
  // We assume this happens in the same dispatcher as any calls to CreateStream
  void OnStreamingShutdown() {
    usb_state_.StopStreaming();
    current_frame_ = nullptr;
    frame_number_ = 0;
    is_streaming_ = false;
    buffers_.Init(nullptr, 0);
    stream_token_.reset();
  }

  zx_status_t FrameRelease(uint32_t frame_offset);

  // Shuts down the stream and unbinds the stream interface
  void CloseStreamOnError(zx_status_t status, std::string call);

  // How the driver gets image data from the usb stack.
  // Multiple requests will complete per frame.
  // The usb thread calls PostRequestCompleteTask, which posts a task to the
  // fidl_dispatch_loop_, which is then handled by HandleRequest, which calls
  // RequestComplete and deals with the task bookkeeping.
  void RequestComplete(usb_request_t* req);
  void PostRequestCompleteTask(usb_request_t* req);
  void HandleRequest(RequestTask* task);
  friend void RequestTask::AsyncTaskHandler(async_dispatcher_t*, async_task_t*, zx_status_t);

  // Handles unbinding the streams to prepare for the driver to be removed.
  // Only called on the the fidl_dispatch_loop_.
  void HandleUnbind(async_dispatcher_t* dispatcher, async::TaskBase* task, zx_status_t status);

  // Helper function called only by RequestComplete.  Ensures that current_frame_
  // exists, and is backed by a buffer if one is available.
  void CheckCurrentFrame();

  // Threading concerns:
  // There are 3 dispatchers that may call into this class.
  // 1) Driver Host, which may call:
  //    -> Unbind/Remove
  //    -> GetChannel
  // 2) Our dispatch loop, which may call
  //    -> All control and stream channel calls
  // 3) The banjo interface, which may call
  //    -> PostRequestCompleteTask
  //
  // To handle conflicts with DdkUnbind and GetChannel, DdkUnbind posts a task
  // to the fidl_dispatch_loop_ to handle unbinding.
  // GetChannel only accesses the control binding and the unbind reply transaction.
  // The async task, the unbind reply transaction and the control binding
  // are all guarded by a lock to prevent concurrent access:
  fbl::Mutex lock_;
  async::TaskMethod<UsbVideoStream, &UsbVideoStream::HandleUnbind> unbind_task_ __TA_GUARDED(lock_);
  std::optional<::ddk::UnbindTxn> unbind_txn_ __TA_GUARDED(lock_) = std::nullopt;
  fidl::Binding<Control> control_binding_ __TA_GUARDED(lock_);
  // To avoid issues with the latter two threads, calls to PostRequestCompleteTask
  // are limited to posting a task to be run on the dispatch loop.
  // This limits potential collisions between the two threads to the req_list_,
  // which is guarded by the req_list_lock_.
  fbl::Mutex req_list_lock_;
  std::deque<RequestTask> req_list_ __TA_GUARDED(req_list_lock_);

  // The other variables are only accessed by methods dispatched by the fidl_dispatch_loop_.
  // current_frame_, frame_number_, buffers_ and is_streaming_ are only valid in the
  // STREAMING and READY states.
  // Current video frame we are populating
  std::unique_ptr<VideoFrame> current_frame_;
  uint32_t frame_number_ = 0;  // used for log messages.
  fzl::VmoPool buffers_;
  // Used for the Start and Stop function.  Start and Stop simply enable/disable
  // the driver to call OnFrameAvailable
  bool is_streaming_ = false;

  UsbState usb_state_;

  // Loop used to run the FIDL server
  std::unique_ptr<async::Loop> fidl_dispatch_loop_;

  zx::eventpair stream_token_;
  fidl::Binding<Stream> stream_binding_;
};

}  // namespace camera::usb_video

#endif  // SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_STREAM_H_
