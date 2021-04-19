// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tun_device.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>

#include "util.h"

namespace network {
namespace tun {

namespace {

bool MacListEquals(const std::vector<fuchsia::net::MacAddress>& l,
                   const std::vector<fuchsia::net::MacAddress>& r) {
  auto li = l.begin();
  auto ri = r.begin();
  for (; li != l.end() && ri != r.end(); li++, ri++) {
    if (memcmp(li->octets.data(), ri->octets.data(), MAC_SIZE) != 0) {
      return false;
    }
  }
  return li == l.end() && ri == r.end();
}

}  // namespace

TunDevice::TunDevice(fit::callback<void(TunDevice*)> teardown,
                     fuchsia::net::tun::DeviceConfig config)
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      teardown_callback_(std::move(teardown)),
      config_(std::move(config)),
      binding_(this) {
  binding_.set_error_handler([this](zx_status_t /*unused*/) { Teardown(); });
}

zx::status<std::unique_ptr<TunDevice>> TunDevice::Create(fit::callback<void(TunDevice*)> teardown,
                                                         fuchsia::net::tun::DeviceConfig config) {
  if (!TryConsolidateDeviceConfig(&config)) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fbl::AllocChecker ac;
  std::unique_ptr<TunDevice> tun(new (&ac) TunDevice(std::move(teardown), std::move(config)));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  if (zx_status_t status = zx::eventpair::create(0, &tun->signals_peer_, &tun->signals_self_);
      status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init failed to create eventpair %s",
            zx_status_get_string(status));
    return zx::error(status);
  }

  zx::status device =
      DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get(), tun->config_.online());
  if (device.is_error()) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init device init failed with %s", device.status_string());
    return device.take_error();
  }
  tun->device_ = std::move(device.value());

  if (tun->config_.has_mac()) {
    zx::status mac = MacAdapter::Create(tun.get(), tun->config_.mac(), false);
    if (mac.is_error()) {
      FX_LOGF(ERROR, "tun", "TunDevice::Init mac init failed with %s", mac.status_string());
      return mac.take_error();
    }
    tun->mac_ = std::move(mac.value());
  }

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
  if (mac_) {
    mac_->TeardownSync();
  }
  loop_.Shutdown();
  FX_VLOG(1, "tun", "TunDevice destroyed");
}

void TunDevice::Bind(fidl::InterfaceRequest<fuchsia::net::tun::Device> req) {
  binding_.Bind(std::move(req), loop_.dispatcher());
}

void TunDevice::Teardown() {
  if (teardown_callback_) {
    teardown_callback_(this);
  }
}

void TunDevice::RunWriteFrame() {
  while (!pending_write_frame_.empty()) {
    auto& pending = pending_write_frame_.front();
    std::optional<fuchsia::net::tun::FrameMetadata> meta;
    if (pending.frame.has_meta()) {
      meta = std::move(pending.frame.meta());
    }
    zx::status avail =
        device_->WriteRxFrame(pending.frame.frame_type(), pending.frame.data(), meta);
    if (avail.is_error()) {
      if (avail.status_value() == ZX_ERR_SHOULD_WAIT && IsBlocking()) {
        return;
      }
      pending.callback(fit::error(avail.status_value()));
    } else {
      if (avail.value() == 0) {
        // Clear the writable signal if no more buffers are available afterwards.
        signals_self_.signal_peer(static_cast<uint32_t>(fuchsia::net::tun::Signals::WRITABLE), 0);
      }
      pending.callback(fit::ok());
    }
    pending_write_frame_.pop();
  }
}

void TunDevice::RunReadFrame() {
  while (!pending_read_frame_.empty()) {
    auto success = device_->TryGetTxBuffer([this](Buffer* buff, size_t avail) {
      std::vector<uint8_t> data;
      zx_status_t status = buff->Read(&data);
      if (status != ZX_OK) {
        FX_LOGF(ERROR, "tun", "Failed to read from tx buffer: %s", zx_status_get_string(status));
      } else if (data.empty()) {
        FX_LOG(WARNING, "tun", "Ignoring empty tx buffer");
      } else {
        auto& callback = pending_read_frame_.front();
        fuchsia::net::tun::Device_ReadFrame_Response rsp;

        rsp.frame.set_data(std::move(data));
        rsp.frame.set_frame_type(buff->frame_type());
        auto meta = buff->TakeMetadata();
        if (meta) {
          rsp.frame.set_meta(std::move(*meta));
        }
        callback(fuchsia::net::tun::Device_ReadFrame_Result::WithResponse(std::move(rsp)));
        pending_read_frame_.pop();
        if (avail == 0) {
          // clear SIGNAL_READABLE if we don't have any more tx buffers.
          signals_self_.signal_peer(static_cast<uint32_t>(fuchsia::net::tun::Signals::READABLE), 0);
        }
      }
    });
    if (!success) {
      if (IsBlocking()) {
        return;
      }
      auto& callback = pending_read_frame_.front();
      callback(fuchsia::net::tun::Device_ReadFrame_Result::WithErr(ZX_ERR_SHOULD_WAIT));
      pending_read_frame_.pop();
    }
  }
}

void TunDevice::RunStateChange() {
  if (!pending_watch_state_) {
    return;
  }
  fuchsia::net::tun::InternalState state;
  state.set_has_session(device_->HasSession());
  {
    if (mac_) {
      fuchsia::net::tun::MacState mac_state;
      mac_->CloneMacState(&mac_state);
      state.set_mac(std::move(mac_state));
    }
  }

  if (last_state_.has_value()) {
    auto& last = last_state_.value();
    // only continue if any changes actually occurred compared to the last observed state
    if (last.has_session() == state.has_session() &&
        (!last.has_mac() ||
         (last.mac().mode() == state.mac().mode() &&
          MacListEquals(last.mac().multicast_filters(), state.mac().multicast_filters())))) {
      return;
    }
  }

  fuchsia::net::tun::InternalState clone;
  state.Clone(&clone);
  // store the last informed state through WatchState
  last_state_ = std::move(clone);
  pending_watch_state_(std::move(state));
  pending_watch_state_ = nullptr;
}

void TunDevice::WriteFrame(fuchsia::net::tun::Frame frame,
                           fuchsia::net::tun::Device::WriteFrameCallback callback) {
  if (pending_write_frame_.size() >= kMaxPendingOps) {
    callback(fit::error(ZX_ERR_NO_RESOURCES));
    return;
  }
  if (!(frame.has_frame_type() && frame.has_data() && !frame.data().empty())) {
    callback(fit::error(ZX_ERR_INVALID_ARGS));
    return;
  }
  pending_write_frame_.emplace(std::move(frame), std::move(callback));
  RunWriteFrame();
}

void TunDevice::ReadFrame(fuchsia::net::tun::Device::ReadFrameCallback callback) {
  if (pending_read_frame_.size() >= kMaxPendingOps) {
    callback(fit::error(ZX_ERR_NO_RESOURCES));
    return;
  }
  pending_read_frame_.push(std::move(callback));
  RunReadFrame();
}

void TunDevice::GetSignals(fuchsia::net::tun::Device::GetSignalsCallback callback) {
  zx::eventpair dup;
  signals_peer_.duplicate(ZX_RIGHTS_BASIC, &dup);
  callback(std::move(dup));
}

void TunDevice::GetState(fuchsia::net::tun::Device::GetStateCallback callback) {
  fuchsia::net::tun::InternalState state;
  state.set_has_session(device_->HasSession());
  if (mac_) {
    fuchsia::net::tun::MacState mac_state;
    mac_->CloneMacState(&mac_state);
    state.set_mac(std::move(mac_state));
  }
  callback(std::move(state));
}

void TunDevice::WatchState(fuchsia::net::tun::Device::WatchStateCallback callback) {
  if (pending_watch_state_) {
    // this is a programming error, we enforce that clients don't do this by closing their channel.
    binding_.Close(ZX_ERR_INTERNAL);
    Teardown();
    return;
  }
  pending_watch_state_ = std::move(callback);
  RunStateChange();
}

void TunDevice::SetOnline(bool online, fuchsia::net::tun::Device::SetOnlineCallback callback) {
  device_->SetOnline(online);
  if (!online) {
    // if we just went offline, we need to complete all pending writes.
    RunWriteFrame();
  }
  callback();
}

void TunDevice::ConnectProtocols(fuchsia::net::tun::Protocols protos) {
  if (device_ && protos.has_network_device()) {
    auto status = device_->Bind(
        fidl::ServerEnd<netdev::Device>(protos.mutable_network_device()->TakeChannel()));
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "tun", "Failed to bind to network device: %s", zx_status_get_string(status));
    }
  }
  if (mac_ && protos.has_mac_addressing()) {
    auto status = mac_->Bind(
        loop_.dispatcher(),
        fidl::ServerEnd<netdev::MacAddressing>(protos.mutable_mac_addressing()->TakeChannel()));
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "tun", "Failed to bind to mac addressing: %s", zx_status_get_string(status));
    }
  }
}

void TunDevice::OnHasSessionsChanged(DeviceAdapter* device) {
  async::PostTask(loop_.dispatcher(), [this]() { RunStateChange(); });
}

void TunDevice::OnTxAvail(DeviceAdapter* device) {
  signals_self_.signal_peer(0, static_cast<uint32_t>(fuchsia::net::tun::Signals::READABLE));
  async::PostTask(loop_.dispatcher(), [this]() { RunReadFrame(); });
}

void TunDevice::OnRxAvail(DeviceAdapter* device) {
  signals_self_.signal_peer(0, static_cast<uint32_t>(fuchsia::net::tun::Signals::WRITABLE));
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
