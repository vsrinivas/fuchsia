// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_DEVICE_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_DEVICE_H_

#include <fuchsia/net/tun/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include <queue>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "device_adapter.h"
#include "mac_adapter.h"
#include "state.h"

namespace network {
namespace tun {

// Implements `fuchsia.net.tun.Device`.
//
// `TunDevice` uses `DeviceAdapter` to fulfill the `fuchsia.net.tun.Device` protocol. All FIDL
// requests are served over its own internally held async dispatcher.
class TunDevice : public fbl::DoublyLinkedListable<std::unique_ptr<TunDevice>>,
                  public fidl::WireServer<fuchsia_net_tun::Device>,
                  public DeviceAdapterParent {
 public:
  static constexpr size_t kMaxPendingOps = fuchsia_net_tun::wire::kMaxPendingOperations;

  // Creates a new `TunDevice` with `config`.
  // `teardown` is called when all the bound client channels are closed.
  static zx::status<std::unique_ptr<TunDevice>> Create(fit::callback<void(TunDevice*)> teardown,
                                                       fuchsia_net_tun::wire::DeviceConfig config);
  ~TunDevice() override;

  // fuchsia.net.tun.Device implementation:
  void WriteFrame(WriteFrameRequestView request, WriteFrameCompleter::Sync& completer) override;
  void ReadFrame(ReadFrameRequestView request, ReadFrameCompleter::Sync& completer) override;
  void GetState(GetStateRequestView request, GetStateCompleter::Sync& completer) override;
  void WatchState(WatchStateRequestView request, WatchStateCompleter::Sync& completer) override;
  void SetOnline(SetOnlineRequestView request, SetOnlineCompleter::Sync& completer) override;
  void ConnectProtocols(ConnectProtocolsRequestView request,
                        ConnectProtocolsCompleter::Sync& completer) override;
  void GetSignals(GetSignalsRequestView request, GetSignalsCompleter::Sync& completer) override;

  InternalState State() const;

  // DeviceAdapterParent implementation:
  const BaseConfig& config() const override { return config_; };
  void OnHasSessionsChanged(DeviceAdapter* device) override;
  void OnTxAvail(DeviceAdapter* device) override;
  void OnRxAvail(DeviceAdapter* device) override;
  // MacAdapterParent implementation:
  void OnMacStateChanged(MacAdapter* adapter) override;

  // Binds `req` to this device.
  // Requests are served over this device's owned loop.
  // NOTE: at this moment only one binding is supported, if the device is already bound the previous
  // channel is closed.
  void Bind(fidl::ServerEnd<fuchsia_net_tun::Device> req);

 private:
  TunDevice(fit::callback<void(TunDevice*)> teardown, DeviceConfig config);
  // Completes a single WriteFrame request. Returns true iff a reply was sent on the completer.
  template <typename F, typename C>
  bool WriteWith(F fn, C& completer);
  // Fulfills pending WriteFrame requests. Returns true iff all pending requests were processed.
  bool RunWriteFrame();
  // Fulfills pending ReadFrame requests.
  void RunReadFrame();
  // Fulfills pending WatchState requests.
  void RunStateChange();
  // Calls the teardown callback.
  void Teardown();
  bool IsBlocking() const { return config_.blocking; }

  fit::callback<void(TunDevice*)> teardown_callback_;
  const DeviceConfig config_;

  async::Loop loop_;
  std::optional<thrd_t> loop_thread_;
  std::optional<fidl::ServerBindingRef<fuchsia_net_tun::Device>> binding_;
  std::unique_ptr<DeviceAdapter> device_;

  // Helper struct to store pending write requests.
  struct PendingWriteRequest {
    // Owned fields of fuchsia_net_tun::wire::Frame.
    fuchsia_hardware_network::wire::FrameType frame_type;
    std::vector<uint8_t> data;
    // FrameMetadata::info is a fidl::VectorView, so we should really be copying it. At the time of
    // writing, it isn't used.
    std::optional<fuchsia_net_tun::wire::FrameMetadata> meta;

    // The FIDL transaction completer.
    WriteFrameCompleter::Async completer;

    PendingWriteRequest(const fuchsia_net_tun::wire::Frame& frame,
                        WriteFrameCompleter::Async completer)
        : completer(std::move(completer)) {
      frame_type = frame.frame_type();
      std::copy(std::begin(frame.data()), std::end(frame.data()), std::back_inserter(data));
      if (frame.has_meta()) {
        meta = frame.meta();
      }
    }
  };

  std::queue<ReadFrameCompleter::Async> pending_read_frame_;
  std::queue<PendingWriteRequest> pending_write_frame_;
  std::optional<WatchStateCompleter::Async> pending_watch_state_;

  std::optional<InternalState> last_state_;

  zx::eventpair signals_self_;
  zx::eventpair signals_peer_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_DEVICE_H_
