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
  binding_.set_error_handler([this](zx_status_t) { Teardown(); });
}

zx_status_t TunDevice::Create(fit::callback<void(TunDevice*)> teardown,
                              fuchsia::net::tun::DeviceConfig config,
                              std::unique_ptr<TunDevice>* out) {
  if (!TryConsolidateDeviceConfig(&config)) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<TunDevice> tun(new TunDevice(std::move(teardown), std::move(config)));
  zx_status_t status = zx::eventpair::create(0, &tun->signals_peer_, &tun->signals_self_);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init failed to create eventpair %s",
            zx_status_get_string(status));
    return status;
  }

  status = DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get(), tun->config_.online(),
                                 &tun->device_);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init device init failed with %s",
            zx_status_get_string(status));
    return status;
  }

  if (tun->config_.has_mac()) {
    status = MacAdapter::Create(tun.get(), tun->config_.mac(), false, &tun->mac_);
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "tun", "TunDevice::Init mac init failed with %s",
              zx_status_get_string(status));
      return status;
    }
  }

  thrd_t thread;
  status = tun->loop_.StartThread("tun-device", &thread);
  if (status != ZX_OK) {
    return status;
  }
  tun->loop_thread_ = thread;

  *out = std::move(tun);
  return ZX_OK;
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
    size_t avail = 0;
    auto& pending = pending_write_frame_.front();
    auto result = device_->WriteRxFrame(
        pending.frame.frame_type(), pending.frame.data(),
        pending.frame.has_meta() ? pending.frame.mutable_meta() : nullptr, &avail);
    if (result == ZX_ERR_SHOULD_WAIT && IsBlocking()) {
      return;
    }
    if (result != ZX_OK) {
      pending.callback(fit::error(result));
    } else {
      if (avail == 0) {
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
      } else {
        auto& callback = pending_read_frame_.front();
        callback(fuchsia::net::tun::Device_ReadFrame_Result::WithErr(ZX_ERR_SHOULD_WAIT));
        pending_read_frame_.pop();
      }
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
    auto status = device_->Bind(protos.mutable_network_device()->TakeChannel());
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "tun", "Failed to bind to network device: %s", zx_status_get_string(status));
    }
  }
  if (mac_ && protos.has_mac_addressing()) {
    auto status = mac_->Bind(loop_.dispatcher(), protos.mutable_mac_addressing()->TakeChannel());
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
