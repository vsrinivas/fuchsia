// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_discovery_manager.h"

#include "garnet/drivers/bluetooth/lib/hci/legacy_low_energy_scanner.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "lib/fxl/logging.h"

#include "remote_device.h"
#include "remote_device_cache.h"

namespace btlib {
namespace gap {

LowEnergyDiscoverySession::LowEnergyDiscoverySession(
    fxl::WeakPtr<LowEnergyDiscoveryManager> manager)
    : active_(true), manager_(manager) {
  FXL_DCHECK(manager_);
}

LowEnergyDiscoverySession::~LowEnergyDiscoverySession() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (active_)
    Stop();
}

void LowEnergyDiscoverySession::SetResultCallback(
    DeviceFoundCallback callback) {
  device_found_callback_ = std::move(callback);
  if (!manager_)
    return;
  for (const auto& cached_device_id : manager_->cached_scan_results()) {
    auto device = manager_->device_cache()->FindDeviceById(cached_device_id);
    FXL_DCHECK(device);
    NotifyDiscoveryResult(*device);
  }
}

void LowEnergyDiscoverySession::Stop() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(active_);
  if (manager_) {
    manager_->RemoveSession(this);
  }
  active_ = false;
}

void LowEnergyDiscoverySession::NotifyDiscoveryResult(
    const RemoteDevice& device) const {
  if (device_found_callback_ &&
      filter_.MatchLowEnergyResult(device.advertising_data(),
                                   device.connectable(), device.rssi())) {
    device_found_callback_(device);
  }
}

void LowEnergyDiscoverySession::NotifyError() {
  active_ = false;
  if (error_callback_)
    error_callback_();
}

LowEnergyDiscoveryManager::LowEnergyDiscoveryManager(
    Mode mode, fxl::RefPtr<hci::Transport> hci, RemoteDeviceCache* device_cache)
    : dispatcher_(async_get_default()),
      device_cache_(device_cache),
      weak_ptr_factory_(this) {
  FXL_DCHECK(hci);
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(device_cache_);

  // We currently do not support the Extended Advertising feature.
  FXL_DCHECK(mode == Mode::kLegacy);

  scanner_ =
      std::make_unique<hci::LegacyLowEnergyScanner>(this, hci, dispatcher_);
}

LowEnergyDiscoveryManager::~LowEnergyDiscoveryManager() {
  // TODO(armansito): Invalidate all known session objects here.
}

void LowEnergyDiscoveryManager::StartDiscovery(SessionCallback callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(callback);
  FXL_LOG(INFO) << "gap: LowEnergyDiscoveryManager: StartDiscovery";

  // If a request to start or stop is currently pending then this one will
  // become pending until the HCI request completes (this does NOT include the
  // state in which we are stopping and restarting scan in between scan
  // periods).
  if (!pending_.empty() ||
      (scanner_->state() == hci::LowEnergyScanner::State::kStopping &&
       sessions_.empty())) {
    FXL_DCHECK(!scanner_->IsScanning());
    pending_.push(std::move(callback));
    return;
  }

  // If a device scan is already in progress, then the request succeeds (this
  // includes the state in which we are stopping and restarting scan in between
  // scan periods).
  if (!sessions_.empty()) {
    // Invoke |callback| asynchronously.
    auto session = AddSession();
    async::PostTask(dispatcher_, [callback = std::move(callback),
                                  session = std::move(session)]() mutable {
      callback(std::move(session));
    });
    return;
  }

  FXL_DCHECK(scanner_->state() == hci::LowEnergyScanner::State::kIdle);

  pending_.push(std::move(callback));
  StartScan();
}

std::unique_ptr<LowEnergyDiscoverySession>
LowEnergyDiscoveryManager::AddSession() {
  // Cannot use make_unique here since LowEnergyDiscoverySession has a private
  // constructor.
  std::unique_ptr<LowEnergyDiscoverySession> session(
      new LowEnergyDiscoverySession(weak_ptr_factory_.GetWeakPtr()));
  FXL_DCHECK(sessions_.find(session.get()) == sessions_.end());
  sessions_.insert(session.get());
  return session;
}

void LowEnergyDiscoveryManager::RemoveSession(
    LowEnergyDiscoverySession* session) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(session);

  // Only active sessions are allowed to call this method. If there is at least
  // one active session object out there, then we MUST be scanning.
  FXL_DCHECK(session->active());

  FXL_DCHECK(sessions_.find(session) != sessions_.end());
  sessions_.erase(session);

  // Stop scanning if the session count has dropped to zero.
  if (sessions_.empty())
    scanner_->StopScan();
}

void LowEnergyDiscoveryManager::OnDeviceFound(
    const hci::LowEnergyScanResult& result, const common::ByteBuffer& data) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  auto device = device_cache_->FindDeviceByAddress(result.address);
  if (!device) {
    device = device_cache_->NewDevice(result.address, result.connectable);
  }
  device->SetLEAdvertisingData(result.rssi, data);

  cached_scan_results_.insert(device->identifier());

  for (const auto& session : sessions_) {
    session->NotifyDiscoveryResult(*device);
  }
}

void LowEnergyDiscoveryManager::OnScanStatus(
    hci::LowEnergyScanner::ScanStatus status) {
  switch (status) {
    case hci::LowEnergyScanner::ScanStatus::kFailed: {
      FXL_LOG(ERROR)
          << "gap: LowEnergyDiscoveryManager: Failed to initiate scan!";

      // Clear all sessions.
      auto sessions = std::move(sessions_);
      for (auto& s : sessions) {
        s->NotifyError();
      }

      // Report failure on all currently pending requests. If any of the
      // callbacks issue a retry the new requests will get re-queued and
      // notified of failure in the same loop here.
      while (!pending_.empty()) {
        auto callback = std::move(pending_.front());
        pending_.pop();

        callback(nullptr);
      }
      break;
    }
    case hci::LowEnergyScanner::ScanStatus::kStarted:
      FXL_VLOG(1) << "gap: LowEnergyDiscoveryManager: Started scanning";

      // Create and register all sessions before notifying the clients. We do
      // this so that the reference count is incremented for all new sessions
      // before the callbacks execute, to prevent a potential case in which a
      // callback stops its session immediately which could cause the reference
      // count to drop the zero before all clients receive their session object.
      if (!pending_.empty()) {
        size_t count = pending_.size();
        std::unique_ptr<LowEnergyDiscoverySession> new_sessions[count];
        std::generate(new_sessions, new_sessions + count,
                      [this] { return AddSession(); });
        for (size_t i = 0; i < count; i++) {
          auto callback = std::move(pending_.front());
          pending_.pop();

          callback(std::move(new_sessions[i]));
        }
      }
      FXL_DCHECK(pending_.empty());
      break;
    case hci::LowEnergyScanner::ScanStatus::kStopped:
      // TODO(armansito): Revise this logic when we support pausing a scan even
      // with active sessions.
      FXL_VLOG(1) << "gap: LowEnergyDiscoveryManager: Stopped scanning";

      cached_scan_results_.clear();

      // Some clients might have requested to start scanning while we were
      // waiting for it to stop. Restart scanning if that is the case.
      if (!pending_.empty()) {
        StartScan();
      }

      break;
    case hci::LowEnergyScanner::ScanStatus::kComplete:
      FXL_VLOG(2) << "gap: LowEnergyDiscoveryManager: end of scan period";
      cached_scan_results_.clear();

      // If |sessions_| is empty this is because sessions were stopped while the
      // scanner was shutting down after the end of the scan period. Restart the
      // scan as long as clients are waiting for it.
      if (!sessions_.empty() || !pending_.empty()) {
        FXL_VLOG(2)
            << "gap: LowEnergyDiscoveryManager: continuing periodic scan";
        StartScan();
      }
      break;
  }
}

void LowEnergyDiscoveryManager::StartScan() {
  auto cb = [self = weak_ptr_factory_.GetWeakPtr()](auto status) {
    if (self)
      self->OnScanStatus(status);
  };

  // TODO(armansito): For now we always do an active scan. When we support the
  // auto-connection procedure we should also implement background scanning
  // using the controller white list.
  // TODO(armansito): Use the appropriate "slow" interval & window values for
  // background scanning.
  // TODO(armansito): A client that is interested in scanning nearby beacons and
  // calculating proximity based on RSSI changes may want to disable duplicate
  // filtering. We generally shouldn't allow this unless a client has the
  // capability for it. Processing all HCI events containing advertising reports
  // will both generate a lot of bus traffic and performing duplicate filtering
  // on the host will take away CPU cycles from other things. It's a valid use
  // case but needs proper management. For now we always make the controller
  // filter duplicate reports.

  // Since we use duplicate filtering, we stop and start the scan periodically
  // to re-process advertisements. We use the minimum required scan period for
  // general discovery (by default; |scan_period_| can be modified, e.g. by unit
  // tests).
  scanner_->StartScan(true /* active */, kLEScanFastInterval, kLEScanFastWindow,
                      true /* filter_duplicates */,
                      hci::LEScanFilterPolicy::kNoWhiteList, scan_period_, cb);
}

}  // namespace gap
}  // namespace btlib
