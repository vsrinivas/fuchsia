// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_DEVICE_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_DEVICE_H_

#include <fuchsia/net/tun/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>

#include <queue>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "buffer.h"
#include "device_adapter.h"
#include "mac_adapter.h"

namespace network {
namespace tun {

// Implements `fuchsia.net.tun.Device`.
//
// `TunDevice` uses `DeviceAdapter` and `MacAdapter` to fulfill the `fuchsia.net.tun.Device`
// protocol. All FIDL requests are served over its own internally held `async::Loop`.
class TunDevice : public fbl::DoublyLinkedListable<std::unique_ptr<TunDevice>>,
                  public fuchsia::net::tun::Device,
                  public DeviceAdapterParent,
                  public MacAdapterParent {
 public:
  static constexpr size_t kMaxPendingOps = fuchsia::net::tun::MAX_PENDING_OPERATIONS;

  // Creates a new `TunDevice` with `config`.
  // `teardown` is called when all the bound client channels are closed.
  // On success, the new device is stored in `out`.
  static zx_status_t Create(fit::callback<void(TunDevice*)> teardown,
                            fuchsia::net::tun::DeviceConfig config,
                            std::unique_ptr<TunDevice>* out);
  ~TunDevice() override;

  // fuchsia.net.tun.Device implementation:
  void WriteFrame(fuchsia::net::tun::Frame frame, WriteFrameCallback callback) override;
  void ReadFrame(ReadFrameCallback callback) override;
  void GetState(GetStateCallback callback) override;
  void WatchState(WatchStateCallback callback) override;
  void SetOnline(bool online, SetOnlineCallback callback) override;
  void ConnectProtocols(fuchsia::net::tun::Protocols protos) override;
  void GetSignals(GetSignalsCallback callback) override;

  // DeviceAdapterParent implementation:
  const fuchsia::net::tun::BaseConfig& config() const override { return config_.base(); };
  void OnHasSessionsChanged(DeviceAdapter* device) override;
  void OnTxAvail(DeviceAdapter* device) override;
  void OnRxAvail(DeviceAdapter* device) override;
  // MacAdapterParent implementation:
  void OnMacStateChanged(MacAdapter* adapter) override;

  // Binds `req` to this device.
  // Requests are served over this device's owned loop.
  // NOTE: at this moment only one binding is supported, if the device is already bound the previous
  // channel is closed.
  void Bind(fidl::InterfaceRequest<fuchsia::net::tun::Device> req);

 private:
  TunDevice(fit::callback<void(TunDevice*)> teardown, fuchsia::net::tun::DeviceConfig config);
  // Fulfills pending WriteFrame requests.
  void RunWriteFrame();
  // Fulfills pending ReadFrame requests.
  void RunReadFrame();
  // Fulfills pending WatchState requests.
  void RunStateChange();
  // Calls the teardown callback.
  void Teardown();
  bool IsBlocking() { return static_cast<bool>(config_.blocking()); }

  async::Loop loop_;
  fit::callback<void(TunDevice*)> teardown_callback_;
  fit::optional<thrd_t> loop_thread_;
  fuchsia::net::tun::DeviceConfig config_;
  fidl::Binding<fuchsia::net::tun::Device> binding_;
  std::unique_ptr<DeviceAdapter> device_;
  std::unique_ptr<MacAdapter> mac_;

  // Helper struct to store pending write requests.
  struct PendingWriteRequest {
    // The frame contained in the request.
    fuchsia::net::tun::Frame frame;
    // The callback to complete the FIDL transaction.
    WriteFrameCallback callback;

    PendingWriteRequest(fuchsia::net::tun::Frame frame, WriteFrameCallback callback)
        : frame(std::move(frame)), callback(std::move(callback)) {}
  };

  std::queue<ReadFrameCallback> pending_read_frame_;
  std::queue<PendingWriteRequest> pending_write_frame_;
  WatchStateCallback pending_watch_state_;
  fit::optional<fuchsia::net::tun::InternalState> last_state_;

  zx::eventpair signals_self_;
  zx::eventpair signals_peer_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_DEVICE_H_
