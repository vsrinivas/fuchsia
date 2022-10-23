// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_USB_DEVICE_STREAM_IMPL_H_
#define SRC_CAMERA_BIN_USB_DEVICE_STREAM_IMPL_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fpromise/result.h>
#include <lib/fpromise/scope.h>
#include <lib/zx/result.h>
#include <zircon/status.h>

#include <memory>
#include <queue>
#include <set>
#include <vector>

#include "src/camera/bin/usb_device/frame_waiter.h"
#include "src/camera/bin/usb_device/util_fidl.h"
#include "src/camera/bin/usb_device/uvc_hack.h"
#include "src/camera/lib/hanging_get_helper/hanging_get_helper.h"

namespace camera {

// Represents a specific stream in a camera device's configuration. Serves multiple clients of the
// camera3.Stream protocol.
class StreamImpl {
 public:
  // Called by the stream when it needs to connect to its associated legacy stream.
  using StreamRequestedCallback =
      fit::function<void(fuchsia::sysmem::BufferCollectionInfo, fuchsia::camera::FrameRate,
                         fidl::InterfaceRequest<fuchsia::camera::Stream>, zx::eventpair)>;
  // Called by the stream when it needs to call BindSharedCollection.
  using AllocatorBindSharedCollectionCallback =
      fit::function<void(fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,
                         fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection>)>;

  StreamImpl(async_dispatcher_t* dispatcher, const fuchsia::camera3::StreamProperties2& properties,
             fidl::InterfaceRequest<fuchsia::camera3::Stream> request,
             StreamRequestedCallback on_stream_requested,
             AllocatorBindSharedCollectionCallback allocator_bind_shared_collection,
             fit::closure on_no_clients, std::optional<std::string> description = std::nullopt);
  ~StreamImpl();

  // Close all client connections with given status as epitaph.
  void CloseAllClients(zx_status_t status);

  fpromise::scope& Scope();

 private:
  // Allocate driver-facing buffer collection.
  zx::result<fuchsia::sysmem::BufferCollectionInfo> Gralloc(
      fuchsia::camera::VideoFormat video_format, uint32_t num_buffers);

  // Called when a client calls Rebind.
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Stream> request);

  // Called if the underlying legacy stream disconnects.
  void OnLegacyStreamDisconnected(zx_status_t status);

  // Remove the client with the given id.
  void RemoveClient(uint64_t id);

  // Called when the legacy stream's OnFrameAvailable event fires.
  void OnFrameAvailable(fuchsia::camera::FrameAvailableEvent info);

  // Renegotiate buffers or opt out of buffer renegotiation for the client with the given id.
  void SetBufferCollection(
      uint64_t id, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle);

  // Error handler for client-facing buffer collection.
  void OnClientBufferCollectionError(zx_status_t status);

  // The following functions are async daisy-chained to execute the steps to initialize the
  // client-facing buffer collection:
  //
  // 1. InitializeClientBufferCollection - Flush token to sysmem to synchronize
  // 2. InitializeSharedCollection - Bind shared collection and set attributes
  // 3. SetClientBufferCollectionConstraints - Set constraints and finish allocation
  // 4. InitializeBuffers - Initialize individual buffers
  //
  // TODO(ernesthua) - Probably could do this as a chain of promises?
  void InitializeClientBufferCollection(
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle);
  void InitializeClientSharedCollection(fuchsia::sysmem::BufferCollectionTokenPtr token);
  void SetClientBufferCollectionConstraints();
  void InitializeClientBuffers(fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info);

  // Allocate the driver-facing buffer collection.
  void AllocateDriverBufferCollection();

  // Connect to UVC camera stream and start streaming.
  void ConnectAndStartStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection_info);

  // Setup our end of the UVC camera stream and connect to the UVC camera stream.
  void ConnectToStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection_info,
                       fuchsia::camera::FrameRate frame_rate);

  // Ask UVC driver to start streaming.
  void StartStreaming();

  // Ask UVC driver to stop streaming.
  void StopStreaming();

  // Our local stub to call BindSharedCollection at DeviceImpl.
  void AllocatorBindSharedCollection(
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle,
      fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request);

  // Process incoming UVC camera frame in preparation for sending to client.
  void ProcessFrameForSend(uint32_t buffer_id);

  // Tell UVC driver that a driver-facing buffer can be returned to the free pool.
  void ReleaseClientFrame(uint32_t buffer_id);

  // Represents a single client connection to the StreamImpl class.
  class Client : public fuchsia::camera3::Stream {
   public:
    Client(StreamImpl& stream, uint64_t id,
           fidl::InterfaceRequest<fuchsia::camera3::Stream> request);
    ~Client() override;

    // Add a frame to the queue of available frames.
    void AddFrame(fuchsia::camera3::FrameInfo2 frame);

    // Send a frame to the client if one is available and has been requested.
    void MaybeSendFrame();

    // Closes |binding_| with the provided |status| epitaph, and removes the client instance from
    // the parent |clients_| map.
    void CloseConnection(zx_status_t status);

    // Add the given token to the client's token queue.
    void ReceiveBufferCollection(fuchsia::sysmem::BufferCollectionTokenHandle token);

    // Update the client's resolution.
    void ReceiveResolution(fuchsia::math::Size coded_size);

    // Update the client's crop region.
    void ReceiveCropRegion(std::unique_ptr<fuchsia::math::RectF> region);

    // Returns a mutable reference to this client's state as a participant in buffer renegotiation.
    bool& Participant();

    // Clears the client's queue of unsent frames.
    void ClearFrames();

   private:
    // Called when the client endpoint of |binding_| is closed.
    void OnClientDisconnected(zx_status_t status);

    // |fuchsia::camera3::Stream|
    void GetProperties(GetPropertiesCallback callback) override;
    void GetProperties2(GetProperties2Callback callback) override;
    void SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) override;
    void WatchCropRegion(WatchCropRegionCallback callback) override;
    void SetResolution(fuchsia::math::Size coded_size) override;
    void WatchResolution(WatchResolutionCallback callback) override;
    void SetBufferCollection(
        fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;
    void WatchBufferCollection(WatchBufferCollectionCallback callback) override;
    void WatchOrientation(WatchOrientationCallback callback) override;
    void GetNextFrame(GetNextFrameCallback callback) override;
    void GetNextFrame2(GetNextFrame2Callback callback) override;
    void Rebind(fidl::InterfaceRequest<Stream> request) override;

    StreamImpl& stream_;
    std::string log_prefix_;
    // Tracking for whether a message has already been logged for the named stage of client
    // progress. This is used to ensure that only one such message is logged per transition per
    // client, as they are high-frequency events that would otherwise spam syslog.
    struct {
      // The client has called camera3::Stream::GetNextFrame.
      bool requested = false;
      // The parent stream has added a frame to this client's available frame queue by calling
      // AddFrame.
      bool available = false;
      // A frame has been sent to the client by invoking the callback provided in a previous call to
      // GetNextFrame.
      bool sent = false;
    } frame_logging_state_;
    uint64_t id_;
    fidl::Binding<fuchsia::camera3::Stream> binding_;
    HangingGetHelper<fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>> buffers_;
    HangingGetHelper<fuchsia::math::Size,
                     fit::function<bool(fuchsia::math::Size, fuchsia::math::Size)>>
        resolution_;
    HangingGetHelper<std::unique_ptr<fuchsia::math::RectF>> crop_region_;
    GetNextFrame2Callback frame_callback_;
    bool participant_ = false;
    std::queue<fuchsia::camera3::FrameInfo2> frames_;
    WatchOrientationCallback orientation_callback_;
  };

  async_dispatcher_t* dispatcher_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  const fuchsia::camera3::StreamProperties2& properties_;
  std::map<uint64_t, std::unique_ptr<Client>> clients_;
  uint64_t client_id_next_ = 1;
  StreamRequestedCallback on_stream_requested_;
  AllocatorBindSharedCollectionCallback allocator_bind_shared_collection_;
  fit::closure on_no_clients_;
  uint32_t max_camping_buffers_ = kUvcHackClientMaxBufferCountForCamping;

  // USB video stream
  fuchsia::camera::StreamPtr stream_;

  // USB video stream token used to signal connection drop
  zx::eventpair stream_token_;

  // Client-facing buffer collection
  fuchsia::sysmem::BufferCollectionInfo_2 client_buffer_collection_info_;
  fuchsia::sysmem::BufferCollectionPtr client_buffer_collection_;

  // Driver-facing buffer collection
  fuchsia::sysmem::BufferCollectionInfo driver_buffer_collection_info_;

  // Maps client-facing buffer ID to the buffer's virtual address.
  std::map<uint32_t, uintptr_t> client_buffer_id_to_virt_addr_;

  // Maps driver-facing buffer ID to the buffer's virtual address.
  std::map<uint32_t, uintptr_t> driver_buffer_id_to_virt_addr_;

  // Buffer ID's of client-facing buffers that are not in use.
  std::queue<uint32_t> client_buffer_free_queue_;

  // Is the USB video driver streaming?
  bool streaming_ = false;

  uint64_t frame_counter_ = 0;
  std::map<uint32_t, std::unique_ptr<FrameWaiter>> frame_waiters_;
  fuchsia::math::Size current_resolution_;
  std::unique_ptr<fuchsia::math::RectF> current_crop_region_;
  fpromise::scope scope_;
  std::string description_;
  friend class Client;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_USB_DEVICE_STREAM_IMPL_H_
