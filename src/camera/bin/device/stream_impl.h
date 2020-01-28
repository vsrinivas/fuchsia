// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_STREAM_IMPL_H_
#define SRC_CAMERA_BIN_DEVICE_STREAM_IMPL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/result.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

class StreamImpl {
 public:
  StreamImpl();
  ~StreamImpl();
  static fit::result<std::unique_ptr<StreamImpl>, zx_status_t> Create(
      fidl::InterfaceHandle<fuchsia::camera2::Stream> legacy_stream);
  zx_status_t Bind(fidl::InterfaceRequest<fuchsia::camera3::Stream> request);

 private:
  void OnLegacyStreamDisconnected(zx_status_t status);

  // Posts a task to remove the client with the given id.
  void PostRemoveClient(uint64_t id);

  class Client : public fuchsia::camera3::Stream {
   public:
    Client(StreamImpl& stream);
    ~Client();
    static fit::result<std::unique_ptr<Client>, zx_status_t> Create(
        StreamImpl& device, uint64_t id, fidl::InterfaceRequest<fuchsia::camera3::Stream> request);

   private:
    void CloseConnection(zx_status_t status);
    void OnClientDisconnected(zx_status_t status);

    // |fuchsia::camera3::Stream|
    void SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) override;
    void WatchCropRegion(WatchCropRegionCallback callback) override;
    void SetResolution(uint32_t index) override;
    void WatchResolution(WatchResolutionCallback callback) override;
    void GetNextFrame(GetNextFrameCallback callback) override;
    void Rebind(fidl::InterfaceRequest<Stream> request) override;

    StreamImpl& stream_;
    uint64_t id_;
    async::Loop loop_;
    fidl::Binding<fuchsia::camera3::Stream> binding_;
  };

  async::Loop loop_;
  fuchsia::camera2::StreamPtr legacy_stream_;
  std::map<uint64_t, std::unique_ptr<Client>> clients_;
  uint64_t client_id_next_ = 1;

  friend class Client;
};

#endif  // SRC_CAMERA_BIN_DEVICE_STREAM_IMPL_H_
