// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_STREAM_IMPL_H_
#define SRC_CAMERA_BIN_DEVICE_STREAM_IMPL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/result.h>
#include <zircon/status.h>

#include <memory>
#include <queue>
#include <set>
#include <vector>

#include "src/camera/lib/hanging_get_helper/hanging_get_helper.h"

// Represents a specific stream in a camera device's configuration. Serves multiple clients of the
// camera3.Stream protocol.
class StreamImpl {
 public:
  using StreamRequestedCallback = fit::function<void(
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,
      fidl::InterfaceRequest<fuchsia::camera2::Stream>, fit::function<void(uint32_t)>)>;
  StreamImpl(fidl::InterfaceRequest<fuchsia::camera3::Stream> request,
             StreamRequestedCallback on_stream_requested, fit::closure on_no_clients);
  ~StreamImpl();

 private:
  // Called if the underlying legacy stream disconnects.
  void OnLegacyStreamDisconnected(zx_status_t status);

  // Posts a task to remove the client with the given id.
  void PostRemoveClient(uint64_t id);

  // Posts a task to add the client with the given id to the queue of frame recipients.
  void PostAddFrameSink(uint64_t id);

  // Called when the legacy stream's OnFrameAvailable event fires.
  void OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info);

  // Posts a task from the client with the given id to renegotiate buffers or opt out of buffer
  // renegotiation.
  void PostSetBufferCollection(uint64_t id,
                               fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token);

  // Sends pending frames to waiting recipients.
  void SendFrames();

  // Represents a single client connection to the StreamImpl class.
  class Client : public fuchsia::camera3::Stream {
   public:
    Client(StreamImpl& stream, uint64_t id,
           fidl::InterfaceRequest<fuchsia::camera3::Stream> request);
    ~Client();

    // Posts a task to transfer ownership of the given frame to this client.
    void PostSendFrame(fuchsia::camera3::FrameInfo frame);

    // Posts a task to add the given token to the client's token queue.
    void PostReceiveBufferCollection(
        fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token);

    // Returns a mutable reference to this client's state as a participant in buffer renegotiation.
    // This state must be managed by the parent stream's thread, not the client thread.
    bool& Participant();

   private:
    // Closes |binding_| with the provided |status| epitaph, and removes the client instance from
    // the parent |clients_| map.
    void CloseConnection(zx_status_t status);

    // Called when the client endpoint of |binding_| is closed.
    void OnClientDisconnected(zx_status_t status);

    // |fuchsia::camera3::Stream|
    void SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) override;
    void WatchCropRegion(WatchCropRegionCallback callback) override;
    void SetResolution(fuchsia::math::Size coded_size) override;
    void WatchResolution(WatchResolutionCallback callback) override;
    void SetBufferCollection(
        fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;
    void WatchBufferCollection(WatchBufferCollectionCallback callback) override;
    void GetNextFrame(GetNextFrameCallback callback) override;
    void Rebind(fidl::InterfaceRequest<Stream> request) override;

    StreamImpl& stream_;
    uint64_t id_;
    async::Loop loop_;
    fidl::Binding<fuchsia::camera3::Stream> binding_;
    camera::HangingGetHelper<fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>>
        buffers_;
    GetNextFrameCallback frame_callback_;
    bool participant_;
  };

  async::Loop loop_;
  fuchsia::camera2::StreamPtr legacy_stream_;
  std::map<uint64_t, std::unique_ptr<Client>> clients_;
  uint64_t client_id_next_ = 1;
  StreamRequestedCallback on_stream_requested_;
  fit::closure on_no_clients_;
  uint32_t max_camping_buffers_ = 0;
  uint64_t frame_counter_ = 0;
  std::queue<uint64_t> frame_sinks_;
  bool frame_sink_warning_sent_ = false;
  std::queue<fuchsia::camera3::FrameInfo> frames_;
  std::map<uint32_t, std::unique_ptr<async::Wait>> frame_waiters_;
  friend class Client;
};

#endif  // SRC_CAMERA_BIN_DEVICE_STREAM_IMPL_H_
