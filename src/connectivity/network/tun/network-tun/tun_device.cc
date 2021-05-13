// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tun_device.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>

#include "buffer.h"

namespace network {
namespace tun {

namespace {
template <typename F>
void WithWireState(F fn, InternalState& state) {
  fuchsia_net_tun::wire::InternalState::Frame_ frame;
  fuchsia_net_tun::wire::InternalState wire_state(
      fidl::ObjectView<fuchsia_net_tun::wire::InternalState::Frame_>::FromExternal(&frame));
  wire_state.set_has_session(fidl::ObjectView<bool>::FromExternal(&state.has_session));

  fuchsia_net_tun::wire::MacState::Frame_ frame_mac;
  fuchsia_net_tun::wire::MacState wire_mac(
      fidl::ObjectView<fuchsia_net_tun::wire::MacState::Frame_>::FromExternal(&frame_mac));
  fidl::VectorView<fuchsia_net::wire::MacAddress> multicast_filters;
  if (state.mac.has_value()) {
    MacState& mac = state.mac.value();
    wire_mac.set_mode(
        fidl::ObjectView<fuchsia_hardware_network::wire::MacFilterMode>::FromExternal(&mac.mode));
    multicast_filters =
        fidl::VectorView<fuchsia_net::wire::MacAddress>::FromExternal(mac.multicast_filters);
    wire_mac.set_multicast_filters(
        fidl::ObjectView<fidl::VectorView<fuchsia_net::wire::MacAddress>>::FromExternal(
            &multicast_filters));
    wire_state.set_mac(fidl::ObjectView<fuchsia_net_tun::wire::MacState>::FromExternal(&wire_mac));
  }

  fn(std::move(wire_state));
}
}  // namespace

TunDevice::TunDevice(fit::callback<void(TunDevice*)> teardown, DeviceConfig config)
    : teardown_callback_(std::move(teardown)),
      config_(std::move(config)),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

zx::status<std::unique_ptr<TunDevice>> TunDevice::Create(
    fit::callback<void(TunDevice*)> teardown, fuchsia_net_tun::wire::DeviceConfig config) {
  std::optional validated_config = DeviceConfig::Create(config);
  if (!validated_config.has_value()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fbl::AllocChecker ac;
  std::unique_ptr<TunDevice> tun(
      new (&ac) TunDevice(std::move(teardown), std::move(validated_config.value())));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  if (zx_status_t status = zx::eventpair::create(0, &tun->signals_peer_, &tun->signals_self_);
      status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init failed to create eventpair %s",
            zx_status_get_string(status));
    return zx::error(status);
  }

  zx::status device = DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get(), tun->config_.online,
                                            tun->config_.mac);
  if (device.is_error()) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init device init failed with %s", device.status_string());
    return device.take_error();
  }
  tun->device_ = std::move(device.value());

  thrd_t thread;
  if (zx_status_t status = tun->loop_.StartThread("tun-device", &thread); status != ZX_OK) {
    return zx::error(status);
  }
  tun->loop_thread_ = thread;

  return zx::ok(std::move(tun));
}

TunDevice::~TunDevice() {
  if (loop_thread_.has_value()) {
    // not allowed to destroy a tun device on the loop thread, will cause deadlock
    ZX_ASSERT(loop_thread_.value() != thrd_current());
  }
  // make sure that device is torn down:
  if (device_) {
    device_->TeardownSync();
  }
  loop_.Shutdown();
  FX_VLOG(1, "tun", "TunDevice destroyed");
}

void TunDevice::Bind(fidl::ServerEnd<fuchsia_net_tun::Device> req) {
  binding_ = fidl::BindServer(loop_.dispatcher(), std::move(req), this,
                              [](TunDevice* impl, fidl::UnbindInfo,
                                 fidl::ServerEnd<fuchsia_net_tun::Device>) { impl->Teardown(); });
}

void TunDevice::Teardown() {
  if (teardown_callback_) {
    teardown_callback_(this);
  }
}

template <typename F, typename C>
bool TunDevice::WriteWith(F fn, C& completer) {
  zx::status avail = fn();
  switch (zx_status_t status = avail.status_value(); status) {
    case ZX_OK:
      if (avail.value() == 0) {
        // Clear the writable signal if no more buffers are available afterwards.
        signals_self_.signal_peer(uint32_t(fuchsia_net_tun::wire::Signals::kWritable), 0);
      }
      completer.ReplySuccess();
      return true;
    case ZX_ERR_SHOULD_WAIT:
      if (IsBlocking()) {
        return false;
      }
      __FALLTHROUGH;
    default:
      completer.ReplyError(status);
      return true;
  }
}

bool TunDevice::RunWriteFrame() {
  while (!pending_write_frame_.empty()) {
    auto& pending = pending_write_frame_.front();
    bool handled = WriteWith(
        [this, &pending]() {
          return device_->WriteRxFrame(pending.frame_type, pending.data, pending.meta);
        },
        pending.completer);
    if (!handled) {
      return false;
    }
    pending_write_frame_.pop();
  }
  return true;
}

void TunDevice::RunReadFrame() {
  while (!pending_read_frame_.empty()) {
    bool success = device_->TryGetTxBuffer([this](TxBuffer& buff, size_t avail) {
      std::vector<uint8_t> data;
      zx_status_t status = buff.Read(data);
      if (status != ZX_OK) {
        FX_LOGF(ERROR, "tun", "Failed to read from tx buffer: %s", zx_status_get_string(status));
        // The error reported here is relayed back to clients as an errored tx frame. There's a
        // contract about specific meanings of errors returned in a tx frame through the netdevice
        // banjo API and it might not match the semantics of the buffer API that generated this
        // error. To avoid the possible impedance mismatch, return a fixed error.
        return ZX_ERR_INTERNAL;
      }
      if (data.empty()) {
        FX_LOG(WARNING, "tun", "Ignoring empty tx buffer");
        return ZX_OK;
      }
      fuchsia_net_tun::wire::Frame::Frame_ fidl_frame;
      fuchsia_net_tun::wire::Frame frame(
          fidl::ObjectView<fuchsia_net_tun::wire::Frame::Frame_>::FromExternal(&fidl_frame));
      fidl::VectorView data_view = fidl::VectorView<uint8_t>::FromExternal(data);
      frame.set_data(fidl::ObjectView<fidl::VectorView<uint8_t>>::FromExternal(&data_view));
      netdev::wire::FrameType frame_type = buff.frame_type();
      frame.set_frame_type(fidl::ObjectView<netdev::wire::FrameType>::FromExternal(&frame_type));
      std::optional meta = buff.TakeMetadata();
      if (meta.has_value()) {
        frame.set_meta(
            fidl::ObjectView<fuchsia_net_tun::wire::FrameMetadata>::FromExternal(&meta.value()));
      }
      pending_read_frame_.front().ReplySuccess(frame);
      pending_read_frame_.pop();
      if (avail == 0) {
        // clear Signals::READABLE if we don't have any more tx buffers.
        signals_self_.signal_peer(uint32_t(fuchsia_net_tun::wire::Signals::kReadable), 0);
      }
      return ZX_OK;
    });
    if (!success) {
      if (IsBlocking()) {
        return;
      }
      pending_read_frame_.front().ReplyError(ZX_ERR_SHOULD_WAIT);
      pending_read_frame_.pop();
    }
  }
}

InternalState TunDevice::State() const {
  InternalState state = {
      .has_session = device_->HasSession(),
  };
  if (device_->mac()) {
    state.mac = device_->mac()->GetMacState();
  }
  return state;
}

void TunDevice::RunStateChange() {
  if (!pending_watch_state_.has_value()) {
    return;
  }

  InternalState state = State();
  // only continue if any changes actually occurred compared to the last observed state
  if (last_state_.has_value() && last_state_.value() == state) {
    return;
  }

  WatchStateCompleter::Async completer = std::exchange(pending_watch_state_, std::nullopt).value();

  WithWireState(
      [&completer](fuchsia_net_tun::wire::InternalState state) {
        completer.Reply(std::move(state));
      },
      state);

  // store the last informed state through WatchState
  last_state_ = std::move(state);
}

void TunDevice::WriteFrame(WriteFrameRequestView request, WriteFrameCompleter::Sync& completer) {
  if (pending_write_frame_.size() >= kMaxPendingOps) {
    completer.ReplyError(ZX_ERR_NO_RESOURCES);
    return;
  }
  if (!(request->frame.has_frame_type() && request->frame.has_data() &&
        !request->frame.data().empty())) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  bool ready = RunWriteFrame();
  if (ready) {
    std::optional<fuchsia_net_tun::wire::FrameMetadata> meta;
    if (request->frame.has_meta()) {
      meta = request->frame.meta();
    }
    bool handled = WriteWith(
        [this, &frame = request->frame, &meta]() {
          return device_->WriteRxFrame(frame.frame_type(), frame.data(), meta);
        },
        completer);
    if (handled) {
      return;
    }
  }
  pending_write_frame_.emplace(request->frame, completer.ToAsync());
}

void TunDevice::ReadFrame(ReadFrameRequestView request, ReadFrameCompleter::Sync& completer) {
  if (pending_read_frame_.size() >= kMaxPendingOps) {
    completer.ReplyError(ZX_ERR_NO_RESOURCES);
    return;
  }
  pending_read_frame_.push(completer.ToAsync());
  RunReadFrame();
}

void TunDevice::GetSignals(GetSignalsRequestView request, GetSignalsCompleter::Sync& completer) {
  zx::eventpair dup;
  signals_peer_.duplicate(ZX_RIGHTS_BASIC, &dup);
  completer.Reply(std::move(dup));
}

void TunDevice::GetState(GetStateRequestView request, GetStateCompleter::Sync& completer) {
  InternalState state = State();
  WithWireState(
      [&completer](fuchsia_net_tun::wire::InternalState state) {
        completer.Reply(std::move(state));
      },
      state);
}

void TunDevice::WatchState(WatchStateRequestView request, WatchStateCompleter::Sync& completer) {
  if (pending_watch_state_) {
    // this is a programming error, we enforce that clients don't do this by closing their channel.
    binding_.value().Close(ZX_ERR_INTERNAL);
    Teardown();
    return;
  }
  pending_watch_state_ = completer.ToAsync();
  RunStateChange();
}

void TunDevice::SetOnline(SetOnlineRequestView request, SetOnlineCompleter::Sync& completer) {
  device_->SetOnline(request->online);
  if (!request->online) {
    // if we just went offline, we need to complete all pending writes.
    RunWriteFrame();
  }
  completer.Reply();
}

void TunDevice::ConnectProtocols(ConnectProtocolsRequestView request,
                                 ConnectProtocolsCompleter::Sync& completer) {
  if (request->protos.has_network_device()) {
    fidl::ServerEnd device = std::move(request->protos.network_device());
    if (device_) {
      zx_status_t status = device_->Bind(std::move(device));
      if (status != ZX_OK) {
        FX_LOGF(ERROR, "tun", "Failed to bind to network device: %s", zx_status_get_string(status));
      }
    }
  }
  if (request->protos.has_mac_addressing()) {
    fidl::ServerEnd mac = std::move(request->protos.mac_addressing());
    if (device_->mac()) {
      zx_status_t status = device_->mac()->Bind(loop_.dispatcher(), std::move(mac));
      if (status != ZX_OK) {
        FX_LOGF(ERROR, "tun", "Failed to bind to mac addressing: %s", zx_status_get_string(status));
      }
    }
  }
}

void TunDevice::OnHasSessionsChanged(DeviceAdapter* device) {
  async::PostTask(loop_.dispatcher(), [this]() { RunStateChange(); });
}

void TunDevice::OnTxAvail(DeviceAdapter* device) {
  signals_self_.signal_peer(0, uint32_t(fuchsia_net_tun::wire::Signals::kReadable));
  async::PostTask(loop_.dispatcher(), [this]() { RunReadFrame(); });
}

void TunDevice::OnRxAvail(DeviceAdapter* device) {
  signals_self_.signal_peer(0, uint32_t(fuchsia_net_tun::wire::Signals::kWritable));
  async::PostTask(loop_.dispatcher(), [this]() { RunWriteFrame(); });
}

void TunDevice::OnMacStateChanged(MacAdapter* adapter) {
  async::PostTask(loop_.dispatcher(), [this]() {
    RunWriteFrame();
    RunStateChange();
  });
}

}  // namespace tun
}  // namespace network
