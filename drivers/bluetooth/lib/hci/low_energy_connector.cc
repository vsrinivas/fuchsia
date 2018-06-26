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

namespace btlib {
namespace hci {

using common::DeviceAddress;
using common::HostError;

LowEnergyConnector::PendingRequest::PendingRequest(
    const DeviceAddress& peer_address, StatusCallback status_callback)
    : canceled(false),
      timed_out(false),
      peer_address(peer_address),
      status_callback(std::move(status_callback)) {}

LowEnergyConnector::LowEnergyConnector(fxl::RefPtr<Transport> hci,
                                       const DeviceAddress& local_address,
                                       async_t* dispatcher,
                                       IncomingConnectionDelegate delegate)
    : dispatcher_(dispatcher),
      hci_(hci),
      local_address_(local_address),
      delegate_(std::move(delegate)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(hci_);
  FXL_DCHECK(delegate_);
  FXL_DCHECK(local_address_.type() == DeviceAddress::Type::kLEPublic);

  auto self = weak_ptr_factory_.GetWeakPtr();
  event_handler_id_ = hci_->command_channel()->AddLEMetaEventHandler(
      kLEConnectionCompleteSubeventCode,
      [self](const auto& event) {
        if (self)
          self->OnConnectionCompleteEvent(event);
      },
      dispatcher_);
}

LowEnergyConnector::~LowEnergyConnector() {
  hci_->command_channel()->RemoveEventHandler(event_handler_id_);
  if (request_pending())
    Cancel();
}

bool LowEnergyConnector::CreateConnection(
    LEOwnAddressType own_address_type, bool use_whitelist,
    const DeviceAddress& peer_address, uint16_t scan_interval,
    uint16_t scan_window,
    const LEPreferredConnectionParameters& initial_parameters,
    StatusCallback status_callback, int64_t timeout_ms) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(status_callback);
  FXL_DCHECK(peer_address.type() != DeviceAddress::Type::kBREDR);
  FXL_DCHECK(timeout_ms > 0);

  if (request_pending())
    return false;

  FXL_DCHECK(!request_timeout_task_.is_pending());
  pending_request_ = PendingRequest(peer_address, std::move(status_callback));

  auto request = CommandPacket::New(kLECreateConnection,
                                    sizeof(LECreateConnectionCommandParams));
  auto params = request->mutable_view()
                    ->mutable_payload<LECreateConnectionCommandParams>();
  params->scan_interval = htole16(scan_interval);
  params->scan_window = htole16(scan_window);
  params->initiator_filter_policy = use_whitelist
                                        ? GenericEnableParam::kEnable
                                        : GenericEnableParam::kDisable;

  // TODO(armansito): Use the resolved address types for <5.0 LE Privacy.
  params->peer_address_type =
      (peer_address.type() == DeviceAddress::Type::kLEPublic)
          ? LEAddressType::kPublic
          : LEAddressType::kRandom;

  params->peer_address = peer_address.value();
  params->own_address_type = own_address_type;
  params->conn_interval_min = htole16(initial_parameters.min_interval());
  params->conn_interval_max = htole16(initial_parameters.max_interval());
  params->conn_latency = htole16(initial_parameters.max_latency());
  params->supervision_timeout =
      htole16(initial_parameters.supervision_timeout());
  params->minimum_ce_length = 0x0000;
  params->maximum_ce_length = 0x0000;

  // HCI Command Status Event will be sent as our completion callback.
  auto self = weak_ptr_factory_.GetWeakPtr();
  auto complete_cb = [self, timeout_ms](auto id, const EventPacket& event) {
    FXL_DCHECK(event.event_code() == kCommandStatusEventCode);

    if (!self)
      return;

    Status status = event.ToStatus();
    if (!status) {
      self->OnCreateConnectionComplete(Status(status), nullptr);
      return;
    }

    // The request was started but has not completed; initiate the command
    // timeout period. NOTE: The request will complete when the controller
    // asynchronously notifies us of with a LE Connection Complete event.
    self->request_timeout_task_.Cancel();
    self->request_timeout_task_.PostDelayed(async_get_default(),
                                            zx::msec(timeout_ms));
  };

  hci_->command_channel()->SendCommand(std::move(request), dispatcher_,
                                       complete_cb, kCommandStatusEventCode);

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

  // At this point we do not know whether the pending connection request has
  // completed or not (it may have completed in the controller but that does not
  // mean that we have processed the corresponding LE Connection Complete
  // event). Below we mark the request as canceled and tell the controller to
  // cancel its pending connection attempt.
  pending_request_->canceled = true;
  pending_request_->timed_out = timed_out;

  request_timeout_task_.Cancel();

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto complete_cb = [self](auto id, const EventPacket& event) {
    if (!self) {
      return;
    }

    Status status = event.ToStatus();
    if (!status) {
      FXL_LOG(WARNING) << "Failed to cancel connection request - status: "
                       << status.ToString();
      return;
    }
  };

  auto cancel = CommandPacket::New(kLECreateConnectionCancel);
  hci_->command_channel()->SendCommand(std::move(cancel), dispatcher_,
                                       complete_cb);
}

void LowEnergyConnector::OnConnectionCompleteEvent(const EventPacket& event) {
  FXL_DCHECK(event.event_code() == kLEMetaEventCode);
  FXL_DCHECK(event.view().payload<LEMetaEventParams>().subevent_code ==
             kLEConnectionCompleteSubeventCode);

  auto params = event.le_event_params<LEConnectionCompleteSubeventParams>();
  FXL_CHECK(params);

  // First check if this event is related to the currently pending request.
  const DeviceAddress peer_address(
      AddressTypeFromHCI(params->peer_address_type), params->peer_address);
  bool matches_pending_request =
      pending_request_ && (pending_request_->peer_address == peer_address);

  Status status(params->status);
  if (!status) {
    if (matches_pending_request) {
      // The "Unknown Connect Identifier" error code is returned if this event
      // was sent due to a successful cancelation via the
      // HCI_LE_Create_Connection_Cancel command (sent by Cancel()).
      if (pending_request_->timed_out) {
        status = Status(HostError::kTimedOut);
      } else if (params->status == StatusCode::kUnknownConnectionId) {
        status = Status(HostError::kCanceled);
      }
      OnCreateConnectionComplete(status, nullptr);
    } else {
      FXL_LOG(WARNING)
          << "Unexpected LE Connection Complete event with error received: "
          << status.ToString();
    }
    return;
  }

  // A new link layer connection was created. Create an object to track this
  // connection.
  LEConnectionParameters connection_params(
      le16toh(params->conn_interval), le16toh(params->conn_latency),
      le16toh(params->supervision_timeout));

  // TODO(armansito): If the connection is incoming, then obtain the advertised
  // address and use that as the local address. We currently use the wrong
  // address, so pairing as slave will fail! (NET-1045)
  auto connection = Connection::CreateLE(
      le16toh(params->connection_handle),
      (params->role == ConnectionRole::kMaster) ? Connection::Role::kMaster
                                                : Connection::Role::kSlave,
      local_address_, peer_address, connection_params, hci_);

  if (matches_pending_request) {
    Status status;
    if (pending_request_->timed_out) {
      status = Status(HostError::kTimedOut);
    } else if (pending_request_->canceled) {
      status = Status(HostError::kCanceled);
    }

    // If we were requested to cancel the connection after the logical link
    // is created we disconnect it.
    if (!status) {
      connection = nullptr;
    }

    OnCreateConnectionComplete(status, std::move(connection));
    return;
  }

  // Pass on to the incoming connection delegate if it didn't match the pending
  // request.
  delegate_(std::move(connection));
}

void LowEnergyConnector::OnCreateConnectionComplete(Status status,
                                                    ConnectionPtr link) {
  FXL_DCHECK(pending_request_);

  request_timeout_task_.Cancel();

  auto status_cb = std::move(pending_request_->status_callback);
  pending_request_.Reset();

  status_cb(status, std::move(link));
}

void LowEnergyConnector::OnCreateConnectionTimeout() {
  FXL_DCHECK(pending_request_);
  FXL_LOG(INFO) << "LE Create Connection timed out: canceling request";

  // TODO(armansito): This should cancel the connection attempt only if the
  // connection attempt isn't using the white list.
  CancelInternal(true);
}

}  // namespace hci
}  // namespace btlib
