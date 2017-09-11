// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_discovery_manager.h"

#include "apps/bluetooth/lib/gap/remote_device.h"
#include "apps/bluetooth/lib/gap/remote_device_cache.h"
#include "apps/bluetooth/lib/hci/legacy_low_energy_scanner.h"
#include "apps/bluetooth/lib/hci/transport.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace bluetooth {
namespace gap {

LowEnergyDiscoverySession::LowEnergyDiscoverySession(
    fxl::WeakPtr<LowEnergyDiscoveryManager> manager)
    : active_(true), manager_(manager) {
  FXL_DCHECK(manager_);

  // Configured by default for the GAP General Discovery procedure.
  SetGeneralDiscoverableFlags();
}

LowEnergyDiscoverySession::~LowEnergyDiscoverySession() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (active_) Stop();
}

void LowEnergyDiscoverySession::SetResultCallback(const DeviceFoundCallback& callback) {
  device_found_callback_ = callback;
  if (!manager_) return;
  for (const auto& cached_device_id : manager_->cached_scan_results()) {
    auto device = manager_->device_cache()->FindDeviceById(cached_device_id);
    FXL_DCHECK(device);
    NotifyDiscoveryResult(*device);
  }
}

void LowEnergyDiscoverySession::Stop() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(active_);
  if (manager_) manager_->RemoveSession(this);
  active_ = false;
}

void LowEnergyDiscoverySession::ResetToDefault() {
  filter_.Reset();

  // Configured by default for the GAP General Discovery procedure.
  SetGeneralDiscoverableFlags();
}

void LowEnergyDiscoverySession::NotifyDiscoveryResult(const RemoteDevice& device) const {
  if (device_found_callback_ && filter_.MatchLowEnergyResult(device.advertising_data(),
                                                             device.connectable(), device.rssi())) {
    device_found_callback_(device);
  }
}

LowEnergyDiscoveryManager::LowEnergyDiscoveryManager(Mode mode, fxl::RefPtr<hci::Transport> hci,
                                                     RemoteDeviceCache* device_cache)
    : task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      device_cache_(device_cache),
      weak_ptr_factory_(this) {
  FXL_DCHECK(hci);
  FXL_DCHECK(task_runner_);
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  FXL_DCHECK(device_cache_);

  // We currently do not support the Extended Advertising feature.
  FXL_DCHECK(mode == Mode::kLegacy);

  scanner_ = std::make_unique<hci::LegacyLowEnergyScanner>(this, hci, task_runner_);
}

LowEnergyDiscoveryManager::~LowEnergyDiscoveryManager() {
  // TODO(armansito): Invalidate all known session objects here.
}

void LowEnergyDiscoveryManager::StartDiscovery(const SessionCallback& callback) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  FXL_DCHECK(callback);

  // If a request to start or stop is currently pending then this one will become pending until the
  // HCI request completes (this does NOT include the state in which we are stopping and restarting
  // scan in between scan periods).
  if (!pending_.empty() ||
      (scanner_->state() == hci::LowEnergyScanner::State::kStopping && sessions_.empty())) {
    FXL_DCHECK(!scanner_->IsScanning());
    pending_.push(callback);
    return;
  }

  // If a device scan is already in progress, then the request succeeds (this includes the state in
  // which we are stopping and restarting scan in between scan periods).
  if (!sessions_.empty()) {
    FXL_DCHECK(scanner_->IsScanning());

    // Invoke |callback| asynchronously.
    auto session = AddSession();
    task_runner_->PostTask(fxl::MakeCopyable(
        [ callback, session = std::move(session) ]() mutable { callback(std::move(session)); }));
    return;
  }

  FXL_DCHECK(scanner_->state() == hci::LowEnergyScanner::State::kIdle);

  pending_.push(callback);
  StartScan();
}

std::unique_ptr<LowEnergyDiscoverySession> LowEnergyDiscoveryManager::AddSession() {
  // Cannot use make_unique here since LowEnergyDiscoverySession has a private constructor.
  std::unique_ptr<LowEnergyDiscoverySession> session(
      new LowEnergyDiscoverySession(weak_ptr_factory_.GetWeakPtr()));
  FXL_DCHECK(sessions_.find(session.get()) == sessions_.end());
  sessions_.insert(session.get());
  return session;
}

void LowEnergyDiscoveryManager::RemoveSession(LowEnergyDiscoverySession* session) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  FXL_DCHECK(session);

  // Only active sessions are allowed to call this method. If there is at least one active session
  // object out there, then we MUST be scanning.
  FXL_DCHECK(session->active());

  FXL_DCHECK(sessions_.find(session) != sessions_.end());
  sessions_.erase(session);

  // Stop scanning if the session count has dropped to zero.
  if (sessions_.empty()) scanner_->StopScan();
}

void LowEnergyDiscoveryManager::OnDeviceFound(const hci::LowEnergyScanResult& result,
                                              const common::ByteBuffer& data) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  auto device = device_cache_->StoreLowEnergyScanResult(result, data);
  FXL_DCHECK(device);

  cached_scan_results_.insert(device->identifier());

  for (const auto& session : sessions_) {
    session->NotifyDiscoveryResult(*device);
  }
}

void LowEnergyDiscoveryManager::OnScanStatus(hci::LowEnergyScanner::Status status) {
  switch (status) {
    case hci::LowEnergyScanner::Status::kFailed:
      FXL_LOG(ERROR) << "gap: LowEnergyDiscoveryManager: Failed to start discovery!";
      FXL_DCHECK(sessions_.empty());

      // Report failure on all currently pending requests. If any of the callbacks issue a retry
      // the new requests will get re-queued and notified of failure in the same loop here.
      while (!pending_.empty()) {
        auto& callback = pending_.front();
        callback(nullptr);

        pending_.pop();
      }
      break;
    case hci::LowEnergyScanner::Status::kStarted:
      FXL_LOG(INFO) << "gap: LowEnergyDiscoveryManager: Started scanning";

      // Create and register all sessions before notifying the clients. We do this so that the
      // reference count is incremented for all new sessions before the callbacks execute, to
      // prevent a potential case in which a callback stops its session immediately which could
      // cause the reference count to drop the zero before all clients receive their session object.
      if (!pending_.empty()) {
        size_t count = pending_.size();
        std::unique_ptr<LowEnergyDiscoverySession> new_sessions[count];
        std::generate(new_sessions, new_sessions + count, [this] { return AddSession(); });
        for (size_t i = 0; i < count; i++) {
          auto& callback = pending_.front();
          callback(std::move(new_sessions[i]));

          pending_.pop();
        }
      }
      FXL_DCHECK(pending_.empty());
      break;
    case hci::LowEnergyScanner::Status::kStopped:
      // TODO(armansito): Revise this logic when we support pausing a scan even with active
      // sessions.
      FXL_LOG(INFO) << "gap: LowEnergyDiscoveryManager: Stopped scanning";

      cached_scan_results_.clear();

      // Some clients might have requested to start scanning while we were waiting for it to stop.
      // Restart scanning if that is the case.
      if (!pending_.empty()) StartScan();
      break;
    case hci::LowEnergyScanner::Status::kComplete:
      FXL_LOG(INFO) << "gap: LowEnergyDiscoveryManager: Continuing periodic scan";
      FXL_DCHECK(!sessions_.empty());
      FXL_DCHECK(pending_.empty());

      cached_scan_results_.clear();

      // The scan period has completed. Restart scanning.
      StartScan();
      break;
  }
}

void LowEnergyDiscoveryManager::StartScan() {
  auto cb = [self = weak_ptr_factory_.GetWeakPtr()](auto status) {
    if (self) self->OnScanStatus(status);
  };

  // TODO(armansito): For now we always do an active scan. When we support the auto-connection
  // procedure we should also implement background scanning using the controller white list.
  // TODO(armansito): Use the appropriate "slow" interval & window values for background scanning.
  // TODO(armansito): A client that is interested in scanning nearby beacons and calculating
  // proximity based on RSSI changes may want to disable duplicate filtering. We generally shouldn't
  // allow this unless a client has the capability for it. Processing all HCI events containing
  // advertising reports will both generate a lot of bus traffic and performing duplicate filtering
  // on the host will take away CPU cycles from other things. It's a valid use case but needs proper
  // management. For now we always make the controller filter duplicate reports.

  // Since we use duplicate filtering, we stop and start the scan periodically to re-process
  // advertisements. We use the minimum required scan period for general discovery (by default;
  // |scan_period_| can be modified, e.g. by unit tests).
  scanner_->StartScan(true /* active */, kLEScanFastInterval, kLEScanFastWindow,
                      true /* filter_duplicates */, hci::LEScanFilterPolicy::kNoWhiteList,
                      scan_period_, cb);
}

}  // namespace gap
}  // namespace bluetooth
