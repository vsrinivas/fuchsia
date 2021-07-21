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

// Forward declaration for test-only friend class used below.
namespace testing {
class TunTest;
}

// TODO(https://fxbug.dev/75528): Remove after soft transition. Aliases are here to allow for easy
// disambiguation.
using LegacyDevice = fidl::WireServer<fuchsia_net_tun::Device>;
using NewDevice = fidl::WireServer<fuchsia_net_tun::Device2>;

// Implements `fuchsia.net.tun.Device`.
//
// `TunDevice` uses `DeviceAdapter` to fulfill the `fuchsia.net.tun.Device` protocol. All FIDL
// requests are served over its own internally held async dispatcher.
class TunDevice : public fbl::DoublyLinkedListable<std::unique_ptr<TunDevice>>,
                  public LegacyDevice,
                  public NewDevice,
                  public DeviceAdapterParent {
 public:
  static constexpr size_t kMaxPendingOps = fuchsia_net_tun::wire::kMaxPendingOperations;
  // TODO(https://fxbug.dev/75528): Remove port fallback after soft transition.
  static constexpr uint8_t kLegacyDefaultPort = 0;

  // Creates a new `TunDevice` with `config`.
  // `teardown` is called when all the bound client channels are closed.
  static zx::status<std::unique_ptr<TunDevice>> Create(
      fit::callback<void(TunDevice*)> teardown, const fuchsia_net_tun::wire::DeviceConfig2& config);
  ~TunDevice() override;

  // TODO(https://fxbug.dev/75528): Remove protocol agnostic forms after soft transition.
  using WriteFrameCallback = fit::callback<void(zx_status_t)>;
  void WriteFrame(fuchsia_net_tun::wire::Frame frame, WriteFrameCallback callback);
  using ReadFrameCallback = fit::callback<void(zx::status<fuchsia_net_tun::wire::Frame>)>;
  void ReadFrame(ReadFrameCallback callback);
  zx_status_t AddPort(const fuchsia_net_tun::wire::DevicePortConfig& config,
                      fidl::ServerEnd<fuchsia_net_tun::Port>& request);
  template <typename F>
  void WriteFrameGeneric(typename F::WriteFrameRequestView& request,
                         typename F::WriteFrameCompleter::Sync& completer);
  template <typename F>
  void ReadFrameGeneric(typename F::ReadFrameRequestView& request,
                        typename F::ReadFrameCompleter::Sync& completer);
  template <typename F>
  void GetSignalsGeneric(typename F::GetSignalsRequestView& request,
                         typename F::GetSignalsCompleter::Sync& completer);

  // fuchsia.net.tun.Device implementation:
  void WriteFrame(LegacyDevice::WriteFrameRequestView request,
                  LegacyDevice::WriteFrameCompleter::Sync& completer) override;
  void ReadFrame(LegacyDevice::ReadFrameRequestView request,
                 LegacyDevice::ReadFrameCompleter::Sync& completer) override;
  void GetState(GetStateRequestView request, GetStateCompleter::Sync& completer) override;
  void WatchState(WatchStateRequestView request, WatchStateCompleter::Sync& completer) override;
  void SetOnline(SetOnlineRequestView request, SetOnlineCompleter::Sync& completer) override;
  void ConnectProtocols(ConnectProtocolsRequestView request,
                        ConnectProtocolsCompleter::Sync& completer) override;
  void GetSignals(LegacyDevice::GetSignalsRequestView request,
                  LegacyDevice::GetSignalsCompleter::Sync& completer) override;

  // fuchsia.net.tun.Device2 implementation:
  void WriteFrame(NewDevice::WriteFrameRequestView request,
                  NewDevice::WriteFrameCompleter::Sync& completer) override;
  void ReadFrame(NewDevice::ReadFrameRequestView request,
                 NewDevice::ReadFrameCompleter::Sync& completer) override;
  void GetSignals(NewDevice::GetSignalsRequestView request,
                  NewDevice::GetSignalsCompleter::Sync& completer) override;
  void AddPort(AddPortRequestView request, AddPortCompleter::Sync& _completer) override;
  void GetDevice(GetDeviceRequestView request, GetDeviceCompleter::Sync& _completer) override;

  // DeviceAdapterParent implementation:
  const BaseDeviceConfig& config() const override { return config_; };
  void OnTxAvail(DeviceAdapter* device) override;
  void OnRxAvail(DeviceAdapter* device) override;

  // Binds `req` to this device.
  // Requests are served over this device's owned loop.
  // NOTE: at this moment only one binding is supported, if the device is already bound the previous
  // channel is closed.
  void Bind(fidl::ServerEnd<fuchsia_net_tun::Device2> req);
  void BindLegacy(fidl::ServerEnd<fuchsia_net_tun::Device> req);

 protected:
  friend testing::TunTest;
  const std::unique_ptr<DeviceAdapter>& adapter() const { return device_; }

 private:
  class Port : public PortAdapterParent, public fidl::WireServer<fuchsia_net_tun::Port> {
   public:
    Port(Port&&) = delete;

    static zx::status<std::unique_ptr<Port>> Create(TunDevice* parent,
                                                    const DevicePortConfig& config);
    // MacAdapterParent implementation:
    void OnMacStateChanged(MacAdapter* adapter) override;

    // PortAdapterParent implementation:
    void OnHasSessionsChanged(PortAdapter& port) override;
    void OnPortStatusChanged(PortAdapter& port, const port_status_t& new_status) override;
    void OnPortDestroyed(PortAdapter& port) override;

    // FIDL port implementation:
    void GetState(GetStateRequestView request, GetStateCompleter::Sync& completer) override;
    void WatchState(WatchStateRequestView request, WatchStateCompleter::Sync& completer) override;
    void SetOnline(SetOnlineRequestView request, SetOnlineCompleter::Sync& completer) override;

    void SetOnline(bool online);
    PortAdapter& adapter() { return *adapter_; }

    void Bind(fidl::ServerEnd<fuchsia_net_tun::Port> req);

   private:
    explicit Port(TunDevice* parent) : parent_(parent) {}
    // Fulfills pending WatchState requests.
    void RunStateChange();
    // Posts |RunStateChange| to run later on parent dispatcher.
    void PostRunStateChange();
    InternalState State() const;

    TunDevice* const parent_;
    std::unique_ptr<PortAdapter> adapter_;
    std::optional<WatchStateCompleter::Async> pending_watch_state_;
    std::optional<InternalState> last_state_;
    std::optional<fidl::ServerBindingRef<fuchsia_net_tun::Port>> binding_;
  };

  TunDevice(fit::callback<void(TunDevice*)> teardown, DeviceConfig config);
  // Completes a single WriteFrame request. Returns true iff a reply was sent on the callback.
  template <typename F, typename C>
  bool WriteWith(F fn, C& callback);
  // Fulfills pending WriteFrame requests. Returns true iff all pending requests were processed.
  bool RunWriteFrame();
  // Fulfills pending ReadFrame requests.
  void RunReadFrame();

  // Calls the teardown callback.
  void Teardown();
  bool IsBlocking() const { return config_.blocking; }

  fit::callback<void(TunDevice*)> teardown_callback_;
  const DeviceConfig config_;

  async::Loop loop_;
  std::optional<thrd_t> loop_thread_;
  std::optional<fidl::ServerBindingRef<fuchsia_net_tun::Device2>> binding_;
  std::optional<fidl::ServerBindingRef<fuchsia_net_tun::Device>> legacy_binding_;
  std::unique_ptr<DeviceAdapter> device_;

  std::array<std::unique_ptr<Port>, MAX_PORTS> ports_;

  // Helper struct to store pending write requests.
  struct PendingWriteRequest {
    // Owned fields of fuchsia_net_tun::wire::Frame.
    uint8_t port_id;
    fuchsia_hardware_network::wire::FrameType frame_type;
    std::vector<uint8_t> data;
    // FrameMetadata::info is a fidl::VectorView, so we should really be copying it. At the time of
    // writing, it isn't used.
    std::optional<fuchsia_net_tun::wire::FrameMetadata> meta;

    // The FIDL transaction callback.
    WriteFrameCallback callback;

    PendingWriteRequest(const fuchsia_net_tun::wire::Frame& frame, WriteFrameCallback callback)
        : callback(std::move(callback)) {
      port_id = frame.has_port() ? frame.port() : kLegacyDefaultPort;
      frame_type = frame.frame_type();
      std::copy(std::begin(frame.data()), std::end(frame.data()), std::back_inserter(data));
      if (frame.has_meta()) {
        meta = frame.meta();
      }
    }
  };

  std::queue<ReadFrameCallback> pending_read_frame_;
  std::queue<PendingWriteRequest> pending_write_frame_;

  zx::eventpair signals_self_;
  zx::eventpair signals_peer_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_DEVICE_H_
