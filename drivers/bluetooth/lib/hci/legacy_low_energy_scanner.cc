// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "legacy_low_energy_scanner.h"

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/hci/advertising_report_parser.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/logging.h"

namespace btlib {
namespace hci {
namespace {

std::string ScanStateToString(LowEnergyScanner::State state) {
  switch (state) {
    case LowEnergyScanner::State::kIdle:
      return "(idle)";
    case LowEnergyScanner::State::kStopping:
      return "(stopping)";
    case LowEnergyScanner::State::kInitiating:
      return "(initiating)";
    case LowEnergyScanner::State::kScanning:
      return "(scanning)";
    default:
      break;
  }

  FXL_NOTREACHED();
  return "(unknown)";
}

}  // namespace

LegacyLowEnergyScanner::PendingScanResult::PendingScanResult(
    const common::DeviceAddress& address) {
  result.address = address;
}

LegacyLowEnergyScanner::LegacyLowEnergyScanner(
    Delegate* delegate,
    fxl::RefPtr<Transport> hci,
    async_dispatcher_t* dispatcher)
    : LowEnergyScanner(delegate, hci, dispatcher), active_scanning_(false) {
  event_handler_id_ = transport()->command_channel()->AddLEMetaEventHandler(
      kLEAdvertisingReportSubeventCode,
      std::bind(&LegacyLowEnergyScanner::OnAdvertisingReportEvent, this,
                std::placeholders::_1),
      this->dispatcher());
}

LegacyLowEnergyScanner::~LegacyLowEnergyScanner() {
  transport()->command_channel()->RemoveEventHandler(event_handler_id_);
}

bool LegacyLowEnergyScanner::StartScan(bool active,
                                       uint16_t scan_interval,
                                       uint16_t scan_window,
                                       bool filter_duplicates,
                                       LEScanFilterPolicy filter_policy,
                                       int64_t period_ms,
                                       ScanStatusCallback callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(callback);
  FXL_DCHECK(period_ms == kPeriodInfinite || period_ms > 0);
  FXL_DCHECK(scan_interval <= kLEScanIntervalMax &&
             scan_interval >= kLEScanIntervalMin);
  FXL_DCHECK(scan_window <= kLEScanIntervalMax &&
             scan_window >= kLEScanIntervalMin);
  FXL_DCHECK(scan_window < scan_interval);

  if (state() != State::kIdle) {
    FXL_LOG(ERROR)
        << "gap: LegacyLowEnergyScanner: cannot start scan while in state: "
        << ScanStateToString(state());
    return false;
  }

  FXL_DCHECK(!scan_cb_);
  FXL_DCHECK(scan_timeout_cb_.IsCanceled());
  FXL_DCHECK(hci_cmd_runner()->IsReady());
  FXL_DCHECK(pending_results_.empty());

  set_state(State::kInitiating);
  active_scanning_ = active;
  scan_cb_ = std::move(callback);

  // HCI_LE_Set_Scan_Parameters
  auto command = CommandPacket::New(kLESetScanParameters,
                                    sizeof(LESetScanParametersCommandParams));
  auto scan_params = command->mutable_view()
                         ->mutable_payload<LESetScanParametersCommandParams>();
  scan_params->scan_type = active ? LEScanType::kActive : LEScanType::kPassive;
  scan_params->scan_interval = htole16(scan_interval);
  scan_params->scan_window = htole16(scan_window);
  scan_params->filter_policy = filter_policy;

  // TODO(armansito): Stop using a public address here when we support LE
  // Privacy. We should *always* use LE Privacy.
  scan_params->own_address_type = LEOwnAddressType::kPublic;
  hci_cmd_runner()->QueueCommand(std::move(command));

  // HCI_LE_Set_Scan_Enable
  command = CommandPacket::New(kLESetScanEnable,
                               sizeof(LESetScanEnableCommandParams));
  auto enable_params =
      command->mutable_view()->mutable_payload<LESetScanEnableCommandParams>();
  enable_params->scanning_enabled = GenericEnableParam::kEnable;
  enable_params->filter_duplicates = filter_duplicates
                                         ? GenericEnableParam::kEnable
                                         : GenericEnableParam::kDisable;

  hci_cmd_runner()->QueueCommand(std::move(command));
  hci_cmd_runner()->RunCommands([this, period_ms](Status status) {
    FXL_DCHECK(scan_cb_);
    FXL_DCHECK(state() == State::kInitiating);

    if (!status) {
      if (status.error() == common::HostError::kCanceled) {
        FXL_VLOG(1) << "gap: LegacyLowEnergyScanner: canceled";
        return;
      }

      auto cb = std::move(scan_cb_);

      FXL_DCHECK(!scan_cb_);
      set_state(State::kIdle);

      FXL_LOG(ERROR) << "gap: LegacyLowEnergyScanner: failed to start scan: "
                     << status.ToString();
      cb(ScanStatus::kFailed);
      return;
    }

    // Set the timeout handler and period.
    if (period_ms != kPeriodInfinite) {
      scan_timeout_cb_.Reset([this] {
        if (IsScanning())
          StopScanInternal(false);
      });
      async::PostDelayedTask(dispatcher(),
          scan_timeout_cb_.callback(),
          zx::msec(period_ms));
    }

    set_state(State::kScanning);

    scan_cb_(ScanStatus::kStarted);
  });

  return true;
}

bool LegacyLowEnergyScanner::StopScan() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  if (state() == State::kStopping || state() == State::kIdle) {
    FXL_VLOG(1)
        << "gap: LegacyLowEnergyScanner: cannot stop scan while in state: "
        << ScanStateToString(state());
    return false;
  }

  // Scan is either being initiated or already running. Cancel any in-flight HCI
  // command sequence.
  if (!hci_cmd_runner()->IsReady())
    hci_cmd_runner()->Cancel();

  // We'll tell the controller to stop scanning even if it is not (this is OK
  // because the command will have no effect; see Core Spec v5.0, Vol 2, Part E,
  // Section 7.8.11, paragraph 4).
  StopScanInternal(true);

  return true;
}

void LegacyLowEnergyScanner::StopScanPeriodForTesting() {
  FXL_DCHECK(IsScanning());
  StopScanInternal(false);
}

void LegacyLowEnergyScanner::StopScanInternal(bool stopped) {
  FXL_DCHECK(scan_cb_);

  scan_timeout_cb_.Cancel();
  set_state(State::kStopping);

  // Notify any pending scan results unless the scan was terminated by the user.
  if (!stopped) {
    for (auto& result : pending_results_) {
      auto& pending = result.second;
      NotifyDeviceFound(pending.result,
                        pending.data.view(0, pending.adv_data_len));
    }
  }

  // Either way clear all results from the previous scan period.
  pending_results_.clear();

  FXL_DCHECK(hci_cmd_runner()->IsReady());

  // Tell the controller to stop scanning.
  auto command = CommandPacket::New(kLESetScanEnable,
                                    sizeof(LESetScanEnableCommandParams));
  auto enable_params =
      command->mutable_view()->mutable_payload<LESetScanEnableCommandParams>();
  enable_params->scanning_enabled = GenericEnableParam::kDisable;
  enable_params->filter_duplicates = GenericEnableParam::kDisable;

  hci_cmd_runner()->QueueCommand(std::move(command));
  hci_cmd_runner()->RunCommands([this, stopped](Status status) {
    FXL_DCHECK(scan_cb_);
    FXL_DCHECK(state() == State::kStopping);

    if (!status) {
      FXL_LOG(WARNING) << "gap: LegacyLowEnergyScanner: Failed to stop scan: "
                       << status.ToString();
      // Something went wrong but there isn't really a meaningful way to
      // recover, so we just fall through and notify the caller with
      // ScanStatus::kFailed instead.
    }

    auto cb = std::move(scan_cb_);
    set_state(State::kIdle);

    cb(!status ? ScanStatus::kFailed
               : (stopped ? ScanStatus::kStopped : ScanStatus::kComplete));
  });
}

void LegacyLowEnergyScanner::OnAdvertisingReportEvent(
    const EventPacket& event) {
  // Drop the event if not requested to scan.
  if (!IsScanning())
    return;

  AdvertisingReportParser parser(event);
  const LEAdvertisingReportData* report;
  int8_t rssi;
  while (parser.GetNextReport(&report, &rssi)) {
    bool needs_scan_rsp = false;
    bool connectable = false;
    switch (report->event_type) {
      case LEAdvertisingEventType::kAdvDirectInd:
        // TODO(armansito): Forward this to a subroutine that can be shared with
        // the LE Directed Advertising eport event handler.
        FXL_VLOG(2) << "gap: LegacyLowEnergyScanner: ignoring ADV_DIRECT_IND";
        continue;
      case LEAdvertisingEventType::kAdvInd:
        connectable = true;
      case LEAdvertisingEventType::kAdvScanInd:
        if (active_scanning_)
          needs_scan_rsp = true;
        break;
      case LEAdvertisingEventType::kScanRsp:
        if (active_scanning_)
          HandleScanResponse(*report, rssi);
        continue;
      default:
        break;
    }

    if (report->length_data > kMaxLEAdvertisingDataLength) {
      FXL_LOG(WARNING)
          << "gap: LegacyLowEnergyScanner: advertising data too long! Ignoring";
      continue;
    }

    common::DeviceAddress address;
    if (!DeviceAddressFromAdvReport(*report, &address))
      continue;

    LowEnergyScanResult result(address, connectable, rssi);

    if (!needs_scan_rsp) {
      NotifyDeviceFound(result,
                        common::BufferView(report->data, report->length_data));
      continue;
    }

    auto iter =
        pending_results_.emplace(address, PendingScanResult(address)).first;
    auto& pending = iter->second;

    // We overwrite the pending result entry with the most recent report, even
    // if one from this device was already pending.
    FXL_DCHECK(address == pending.result.address);
    pending.result.connectable = connectable;
    pending.result.rssi = rssi;
    pending.adv_data_len = report->length_data;
    pending.data.Write(report->data, report->length_data);
  }
}

void LegacyLowEnergyScanner::HandleScanResponse(
    const LEAdvertisingReportData& report,
    int8_t rssi) {
  common::DeviceAddress address;
  if (!DeviceAddressFromAdvReport(report, &address))
    return;

  auto iter = pending_results_.find(address);
  if (iter == pending_results_.end()) {
    FXL_VLOG(1)
        << "gap: LegacyLowEnergyScanner: Dropping unmatched scan response";
    return;
  }

  if (report.length_data > kMaxLEAdvertisingDataLength) {
    FXL_LOG(WARNING)
        << "gap: LegacyLowEnergyScanner: scan response too long! Ignoring";
    return;
  }
  auto& pending = iter->second;
  FXL_DCHECK(address == pending.result.address);

  // Use the newer RSSI.
  pending.result.rssi = rssi;

  // Append the scan response to the pending advertising data.
  pending.data.Write(report.data, report.length_data, pending.adv_data_len);

  NotifyDeviceFound(
      pending.result,
      pending.data.view(0, pending.adv_data_len + report.length_data));
  pending_results_.erase(iter);
}

void LegacyLowEnergyScanner::NotifyDeviceFound(
    const LowEnergyScanResult& result,
    const common::ByteBuffer& data) {
  delegate()->OnDeviceFound(result, data);
}

}  // namespace hci
}  // namespace btlib
