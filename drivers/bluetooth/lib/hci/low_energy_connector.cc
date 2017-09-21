// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_connector.h"

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/hci/defaults.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace bluetooth {
namespace hci {

LowEnergyConnector::PendingRequest::PendingRequest(const common::DeviceAddress& peer_address,
                                                   uint16_t interval_min, uint16_t interval_max,
                                                   const ResultCallback& result_callback)
    : canceled(false),
      timed_out(false),
      peer_address(peer_address),
      interval_min(interval_min),
      interval_max(interval_max),
      result_callback(result_callback) {}

LowEnergyConnector::LowEnergyConnector(fxl::RefPtr<Transport> hci,
                                       fxl::RefPtr<fxl::TaskRunner> task_runner,
                                       const ConnectionDelegate& delegate)
    : task_runner_(task_runner), hci_(hci), delegate_(delegate), weak_ptr_factory_(this) {
  FXL_DCHECK(task_runner_);
  FXL_DCHECK(hci_);
  FXL_DCHECK(delegate_);

  auto self = weak_ptr_factory_.GetWeakPtr();
  event_handler_id_ =
      hci_->command_channel()->AddLEMetaEventHandler(kLEConnectionCompleteSubeventCode,
                                                     [self](const auto& event) {
                                                       if (self)
                                                         self->OnConnectionCompleteEvent(event);
                                                     },
                                                     task_runner_);
}

LowEnergyConnector::~LowEnergyConnector() {
  hci_->command_channel()->RemoveEventHandler(event_handler_id_);
  if (request_pending()) Cancel();
}

bool LowEnergyConnector::CreateConnection(hci::LEOwnAddressType own_address_type,
                                          bool use_whitelist,
                                          const common::DeviceAddress& peer_address,
                                          uint16_t scan_interval, uint16_t scan_window,
                                          const Connection::LowEnergyParameters& initial_parameters,
                                          const ResultCallback& result_callback,
                                          int64_t timeout_ms) {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  FXL_DCHECK(result_callback);
  FXL_DCHECK(peer_address.type() != common::DeviceAddress::Type::kBREDR);
  FXL_DCHECK(timeout_ms > 0);

  if (request_pending()) return false;

  FXL_DCHECK(request_timeout_cb_.IsCanceled());
  pending_request_ = PendingRequest(peer_address, initial_parameters.interval_min(),
                                    initial_parameters.interval_max(), result_callback);

  auto request = CommandPacket::New(kLECreateConnection, sizeof(LECreateConnectionCommandParams));
  auto params = request->mutable_view()->mutable_payload<LECreateConnectionCommandParams>();
  params->scan_interval = htole16(scan_interval);
  params->scan_window = htole16(scan_window);
  params->initiator_filter_policy =
      use_whitelist ? GenericEnableParam::kEnable : GenericEnableParam::kDisable;

  // TODO(armansito): Use the resolved address types for <5.0 LE Privacy.
  params->peer_address_type = (peer_address.type() == common::DeviceAddress::Type::kLEPublic)
                                  ? hci::LEAddressType::kPublic
                                  : hci::LEAddressType::kRandom;

  params->peer_address = peer_address.value();
  params->own_address_type = own_address_type;
  params->conn_interval_min = htole16(initial_parameters.interval_min());
  params->conn_interval_max = htole16(initial_parameters.interval_max());
  params->conn_latency = htole16(initial_parameters.latency());
  params->supervision_timeout = htole16(initial_parameters.supervision_timeout());
  params->minimum_ce_length = 0x0000;
  params->maximum_ce_length = 0x0000;

  // HCI Command Status Event will be sent as our completion callback.
  auto self = weak_ptr_factory_.GetWeakPtr();
  auto complete_cb = [self, timeout_ms](auto id, const EventPacket& event) {
    FXL_DCHECK(event.event_code() == kCommandStatusEventCode);

    if (!self) return;

    Status hci_status = event.view().payload<CommandStatusEventParams>().status;
    if (hci_status != Status::kSuccess) {
      self->OnCreateConnectionComplete(Result::kFailed, hci_status);
      return;
    }

    // The request was started but has not completed; initiate the command timeout period.
    // NOTE: The request will complete when the controller asynchronously notifies us of
    // with a LE Connection Complete event.
    self->request_timeout_cb_.Reset([self] {
      // If |self| was destroyed then this callback should have been canceled.
      FXL_DCHECK(self);
      self->OnCreateConnectionTimeout();
    });
    self->task_runner_->PostDelayedTask(self->request_timeout_cb_.callback(),
                                        fxl::TimeDelta::FromMilliseconds(timeout_ms));
  };

  hci_->command_channel()->SendCommand(std::move(request), task_runner_, complete_cb, nullptr,
                                       kCommandStatusEventCode);

  return true;
}

void LowEnergyConnector::Cancel() {
  CancelInternal(false);
}

void LowEnergyConnector::CancelInternal(bool timed_out) {
  FXL_DCHECK(request_pending());

  if (pending_request_->canceled) {
    FXL_LOG(WARNING) << "Connection attempt already canceled!";
    return;
  }

  // At this point we do not know whether the pending connection request has completed or not (it
  // may have completed in the controller but that does not mean that we have processed the
  // corresponding LE Connection Complete event). Below we mark the request as canceled and tell the
  // controller to cancel its pending connection attempt.
  pending_request_->canceled = true;
  pending_request_->timed_out = timed_out;

  if (!request_timeout_cb_.IsCanceled()) request_timeout_cb_.Cancel();

  auto complete_cb = [](auto id, const EventPacket& event) {
    Status status = event.return_params<SimpleReturnParams>()->status;
    if (status != Status::kSuccess) {
      FXL_LOG(WARNING) << "Failed to cancel connection request - status: " << std::hex << status;
      return;
    }
  };

  auto cancel = CommandPacket::New(kLECreateConnectionCancel);
  hci_->command_channel()->SendCommand(std::move(cancel), task_runner_, complete_cb);
}

void LowEnergyConnector::OnConnectionCompleteEvent(const EventPacket& event) {
  FXL_DCHECK(event.event_code() == kLEMetaEventCode);
  FXL_DCHECK(event.view().payload<LEMetaEventParams>().subevent_code ==
             kLEConnectionCompleteSubeventCode);

  auto params = event.le_event_params<LEConnectionCompleteSubeventParams>();

  // First check if this event is related to the currently pending request.
  const common::DeviceAddress peer_address(AddressTypeFromHCI(params->peer_address_type),
                                           params->peer_address);
  bool matches_pending_request =
      pending_request_ && (pending_request_->peer_address == peer_address);

  if (params->status != hci::Status::kSuccess) {
    if (matches_pending_request) {
      // The "Unknown Connect Identifier" error code is returned if this event was sent due to a
      // successful cancelation via the HCI_LE_Create_Connection_Cancel command (sent by Cancel()).
      OnCreateConnectionComplete(
          params->status == Status::kUnknownConnectionId ? Result::kCanceled : Result::kFailed,
          pending_request_->timed_out ? Status::kCommandTimeout : params->status);
    } else {
      FXL_LOG(WARNING) << "Unexpected LE Connection Complete event with error received: 0x"
                       << std::hex << params->status;
    }
    return;
  }

  // Use the pending request to populate the min/max interval parameters. If this connection was not
  // due to a pending request we assign the default values.
  uint16_t interval_min, interval_max;
  if (matches_pending_request) {
    interval_min = pending_request_->interval_min;
    interval_max = pending_request_->interval_max;
  } else {
    interval_min = defaults::kLEConnectionIntervalMin;
    interval_max = defaults::kLEConnectionIntervalMax;
  }

  // A new link layer connection was created. Create an object to track this connection.
  Connection::LowEnergyParameters connection_params(
      interval_min, interval_max, le16toh(params->conn_interval), le16toh(params->conn_latency),
      le16toh(params->supervision_timeout));

  auto connection = std::make_unique<Connection>(le16toh(params->connection_handle),
                                                 (params->role == LEConnectionRole::kMaster)
                                                     ? Connection::Role::kMaster
                                                     : Connection::Role::kSlave,
                                                 peer_address, connection_params, hci_);

  if (matches_pending_request) {
    bool canceled = pending_request_->canceled;
    bool timed_out = pending_request_->timed_out;
    OnCreateConnectionComplete(canceled ? Result::kCanceled : Result::kSuccess,
                               timed_out ? Status::kCommandTimeout : Status::kSuccess);

    // If we were requested to cancel the connection after the link layer connection was created we
    // destroy the connection here.
    if (canceled) return;
  }

  // Pass the connection on to the delegate.
  delegate_(std::move(connection));
}

void LowEnergyConnector::OnCreateConnectionComplete(Result result, Status hci_status) {
  FXL_DCHECK(pending_request_);

  if (!request_timeout_cb_.IsCanceled()) request_timeout_cb_.Cancel();

  auto result_cb = pending_request_->result_callback;
  pending_request_.Reset();

  result_cb(result, hci_status);
}

void LowEnergyConnector::OnCreateConnectionTimeout() {
  FXL_DCHECK(pending_request_);
  FXL_LOG(INFO) << "LE Create Connection timed out: canceling request";

  // TODO(armansito): This should cancel the connection attempt only if the connection attempt isn't
  // using the white list.
  CancelInternal(true);
}

}  // namespace hci
}  // namespace bluetooth
