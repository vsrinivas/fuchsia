// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_controller.h"

#include <endian.h>

#include "apps/bluetooth/lib/common/packet_view.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/util.h"
#include "apps/bluetooth/lib/testing/fake_device.h"
#include "lib/fxl/strings/string_printf.h"

namespace bluetooth {
namespace testing {
namespace {

template <typename NUM_TYPE, typename ENUM_TYPE>
void SetBit(NUM_TYPE* num_type, ENUM_TYPE bit) {
  *num_type |= static_cast<NUM_TYPE>(bit);
}

hci::LEPeerAddressType ToPeerAddrType(common::DeviceAddress::Type type) {
  hci::LEPeerAddressType result = hci::LEPeerAddressType::kAnonymous;

  switch (type) {
    case common::DeviceAddress::Type::kLEPublic:
      result = hci::LEPeerAddressType::kPublic;
      break;
    case common::DeviceAddress::Type::kLERandom:
      result = hci::LEPeerAddressType::kRandom;
      break;
    default:
      break;
  }

  return result;
}

}  // namespace

FakeController::Settings::Settings() {
  ApplyDefaults();
}

void FakeController::Settings::ApplyDefaults() {
  std::memset(this, 0, sizeof(*this));
  hci_version = hci::HCIVersion::k5_0;
  num_hci_command_packets = 1;
}

void FakeController::Settings::ApplyLEOnlyDefaults() {
  ApplyDefaults();

  le_acl_data_packet_length = 512;
  le_total_num_acl_data_packets = 1;

  SetBit(&lmp_features_page0, hci::LMPFeature::kBREDRNotSupported);
  SetBit(&lmp_features_page0, hci::LMPFeature::kLESupported);
  SetBit(&lmp_features_page0, hci::LMPFeature::kExtendedFeatures);

  SetBit(supported_commands, hci::SupportedCommand::kDisconnect);
  SetBit(supported_commands + 5, hci::SupportedCommand::kSetEventMask);
  SetBit(supported_commands + 5, hci::SupportedCommand::kReset);
  SetBit(supported_commands + 14, hci::SupportedCommand::kReadLocalVersionInformation);
  SetBit(supported_commands + 14, hci::SupportedCommand::kReadLocalSupportedFeatures);
  SetBit(supported_commands + 14, hci::SupportedCommand::kReadLocalExtendedFeatures);
  SetBit(supported_commands + 24, hci::SupportedCommand::kWriteLEHostSupport);
  SetBit(supported_commands + 25, hci::SupportedCommand::kLESetEventMask);
  SetBit(supported_commands + 25, hci::SupportedCommand::kLEReadBufferSize);
  SetBit(supported_commands + 25, hci::SupportedCommand::kLEReadLocalSupportedFeatures);
  SetBit(supported_commands + 26, hci::SupportedCommand::kLECreateConnection);
  SetBit(supported_commands + 26, hci::SupportedCommand::kLECreateConnectionCancel);
}

void FakeController::Settings::ApplyLegacyLEConfig() {
  ApplyLEOnlyDefaults();

  hci_version = hci::HCIVersion::k4_2;

  SetBit(supported_commands + 26, hci::SupportedCommand::kLESetScanParameters);
  SetBit(supported_commands + 26, hci::SupportedCommand::kLESetScanEnable);
}

void FakeController::Settings::ApplyLEConfig() {
  ApplyLEOnlyDefaults();

  SetBit(&le_features, hci::LESupportedFeature::kLEExtendedAdvertising);
}

FakeController::LEScanState::LEScanState()
    : enabled(false),
      scan_type(hci::LEScanType::kPassive),
      scan_interval(0),
      scan_window(0),
      filter_duplicates(false),
      filter_policy(hci::LEScanFilterPolicy::kNoWhiteList) {}

FakeController::FakeController(mx::channel cmd_channel, mx::channel acl_data_channel)
    : FakeControllerBase(std::move(cmd_channel), std::move(acl_data_channel)),
      next_conn_handle_(0u),
      le_connect_pending_(false) {}

FakeController::~FakeController() {
  if (IsStarted()) Stop();
}

void FakeController::SetDefaultResponseStatus(hci::OpCode opcode, hci::Status status) {
  FXL_DCHECK(status != hci::Status::kSuccess);
  default_status_map_[opcode] = status;
}

void FakeController::ClearDefaultResponseStatus(hci::OpCode opcode) {
  default_status_map_.erase(opcode);
}

void FakeController::AddLEDevice(std::unique_ptr<FakeDevice> le_device) {
  le_devices_.push_back(std::move(le_device));
}

void FakeController::SetScanStateCallback(const ScanStateCallback& callback,
                                          fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(callback);
  FXL_DCHECK(task_runner);

  scan_state_cb_ = callback;
  scan_state_cb_runner_ = task_runner;
}

void FakeController::SetConnectionStateCallback(const ConnectionStateCallback& callback,
                                                fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(callback);
  FXL_DCHECK(task_runner);

  conn_state_cb_ = callback;
  conn_state_cb_runner_ = task_runner;
}

void FakeController::RespondWithCommandComplete(hci::OpCode opcode, const void* return_params,
                                                uint8_t return_params_size) {
  // Either both are zero or neither is.
  FXL_DCHECK(!!return_params == !!return_params_size);

  common::DynamicByteBuffer buffer(sizeof(hci::EventHeader) +
                                   sizeof(hci::CommandCompleteEventParams) + return_params_size);
  common::MutablePacketView<hci::EventHeader> event(&buffer,
                                                    buffer.size() - sizeof(hci::EventHeader));
  event.mutable_header()->event_code = hci::kCommandCompleteEventCode;
  event.mutable_header()->parameter_total_size = buffer.size() - sizeof(hci::EventHeader);

  auto payload = event.mutable_payload<hci::CommandCompleteEventParams>();
  payload->num_hci_command_packets = settings_.num_hci_command_packets;
  payload->command_opcode = htole16(opcode);
  std::memcpy(payload->return_parameters, return_params, return_params_size);

  SendCommandChannelPacket(buffer);
}

void FakeController::RespondWithCommandStatus(hci::OpCode opcode, hci::Status status) {
  common::StaticByteBuffer<sizeof(hci::EventHeader) + sizeof(hci::CommandStatusEventParams)> buffer;
  common::MutablePacketView<hci::EventHeader> event(&buffer,
                                                    buffer.size() - sizeof(hci::EventHeader));
  event.mutable_header()->event_code = hci::kCommandStatusEventCode;
  event.mutable_header()->parameter_total_size = buffer.size() - sizeof(hci::EventHeader);

  auto payload = event.mutable_payload<hci::CommandStatusEventParams>();
  payload->status = status;
  payload->num_hci_command_packets = settings_.num_hci_command_packets;
  payload->command_opcode = htole16(opcode);

  SendCommandChannelPacket(buffer);
}

void FakeController::SendEvent(hci::EventCode event_code, const void* params, uint8_t params_size) {
  FXL_DCHECK(!!params == !!params_size);

  common::DynamicByteBuffer buffer(sizeof(hci::EventHeader) + params_size);
  common::MutablePacketView<hci::EventHeader> event(&buffer, params_size);

  event.mutable_header()->event_code = event_code;
  event.mutable_header()->parameter_total_size = params_size;
  event.mutable_payload_data().Write(reinterpret_cast<const uint8_t*>(params), params_size);

  SendCommandChannelPacket(buffer);
}

void FakeController::SendLEMetaEvent(hci::EventCode subevent_code, const void* params,
                                     uint8_t params_size) {
  FXL_DCHECK(!!params == !!params_size);
  common::DynamicByteBuffer buffer(sizeof(hci::LEMetaEventParams) + params_size);
  buffer[0] = subevent_code;
  buffer.Write(static_cast<const uint8_t*>(params), params_size, 1);

  SendEvent(hci::kLEMetaEventCode, buffer.data(), buffer.size());
}

bool FakeController::MaybeRespondWithDefaultStatus(hci::OpCode opcode) {
  auto iter = default_status_map_.find(opcode);
  if (iter == default_status_map_.end()) return false;

  FXL_LOG(INFO) << fxl::StringPrintf(
      "hci: FakeController: Responding with error (command: 0x%04x, status: 0x%02x", opcode,
      iter->second);

  hci::SimpleReturnParams params;
  params.status = iter->second;
  RespondWithCommandComplete(opcode, &params, sizeof(params));
  return true;
}

void FakeController::SendAdvertisingReports() {
  if (!le_scan_state_.enabled || le_devices_.empty()) return;

  for (const auto& device : le_devices_) {
    // We want to send scan response packets only during an active scan and if the device is
    // scannable.
    bool need_scan_rsp =
        (le_scan_state().scan_type == hci::LEScanType::kActive) && device->scannable();
    SendCommandChannelPacket(
        device->CreateAdvertisingReportEvent(need_scan_rsp && device->should_batch_reports()));

    // If the original report did not include a scan response then we send it as a separate event.
    if (need_scan_rsp && !device->should_batch_reports()) {
      SendCommandChannelPacket(device->CreateScanResponseReportEvent());
    }
  }

  // We'll send new reports for the same devices if duplicate filtering is disabled.
  if (!le_scan_state_.filter_duplicates) {
    task_runner()->PostTask([this] { SendAdvertisingReports(); });
  }
}

void FakeController::NotifyConnectionState(const common::DeviceAddress& addr, bool connected,
                                           bool canceled) {
  if (!conn_state_cb_) return;

  FXL_DCHECK(conn_state_cb_runner_);
  conn_state_cb_runner_->PostTask(
      [ addr, connected, canceled, cb = conn_state_cb_ ] { cb(addr, connected, canceled); });
}

void FakeController::OnLECreateConnectionCommandReceived(
    const hci::LECreateConnectionCommandParams& params) {
  // Cannot issue this command while a request is already pending.
  if (le_connect_pending_) {
    RespondWithCommandStatus(hci::kLECreateConnection, hci::Status::kCommandDisallowed);
    return;
  }

  common::DeviceAddress::Type addr_type = hci::AddressTypeFromHCI(params.peer_address_type);
  FXL_DCHECK(addr_type != common::DeviceAddress::Type::kBREDR);

  const common::DeviceAddress peer_address(addr_type, params.peer_address);

  // Find the device that matches the requested address.
  FakeDevice* device = nullptr;
  for (const auto& dev : le_devices_) {
    if (dev->address() == peer_address) {
      device = dev.get();
      break;
    }
  }

  hci::Status status = hci::Status::kSuccess;

  if (device) {
    if (device->connected())
      status = hci::Status::kConnectionAlreadyExists;
    else
      status = device->connect_status();
  }

  // First send the Command Status response.
  RespondWithCommandStatus(hci::kLECreateConnection, status);

  // If we just sent back an error status then the operation is complete.
  if (status != hci::Status::kSuccess) return;

  le_connect_pending_ = true;
  pending_le_connect_addr_ = peer_address;

  // The procedure was initiated successfully but the device cannot be connected because it either
  // doesn't exist or isn't connectable.
  if (!device || !device->connectable()) {
    FXL_LOG(INFO) << "Requested fake device cannot be connected; request will time out";
    return;
  }

  if (next_conn_handle_ == 0x0FFF) {
    // Ran out of handles
    status = hci::Status::kConnectionLimitExceeded;
  } else {
    status = device->connect_response();
  }

  hci::LEConnectionCompleteSubeventParams response;
  std::memset(&response, 0, sizeof(response));

  response.status = status;
  response.peer_address = params.peer_address;
  response.peer_address_type = ToPeerAddrType(addr_type);

  if (status == hci::Status::kSuccess) {
    uint16_t interval_min = le16toh(params.conn_interval_min);
    uint16_t interval_max = le16toh(params.conn_interval_max);
    uint16_t interval = interval_min + ((interval_max - interval_min) / 2);

    hci::Connection::LowEnergyParameters conn_params(interval_min, interval_max, interval,
                                                     le16toh(params.conn_latency),
                                                     le16toh(params.supervision_timeout));
    device->set_le_params(conn_params);

    response.conn_latency = params.conn_latency;
    response.conn_interval = le16toh(interval);
    response.supervision_timeout = params.supervision_timeout;

    response.role = hci::LEConnectionRole::kMaster;

    hci::ConnectionHandle handle = ++next_conn_handle_;
    response.connection_handle = htole16(handle);
  }

  pending_le_connect_rsp_.Reset([response, device, this] {
    le_connect_pending_ = false;

    if (response.status == hci::Status::kSuccess) {
      bool notify = !device->connected();
      device->AddLink(le16toh(response.connection_handle));
      if (notify && device->connected()) NotifyConnectionState(device->address(), true);
    }

    SendLEMetaEvent(hci::kLEConnectionCompleteSubeventCode, &response, sizeof(response));
  });

  task_runner()->PostDelayedTask(
      [conn_cb = pending_le_connect_rsp_.callback()] { conn_cb(); },
      fxl::TimeDelta::FromMilliseconds(device->connect_response_period_ms()));
}

void FakeController::OnDisconnectCommandReceived(const hci::DisconnectCommandParams& params) {
  hci::ConnectionHandle handle = le16toh(params.connection_handle);

  // Find the device that matches the disconnected handle.
  FakeDevice* device = nullptr;
  for (const auto& dev : le_devices_) {
    if (dev->HasLink(handle)) {
      device = dev.get();
      break;
    }
  }

  if (!device) {
    RespondWithCommandStatus(hci::kDisconnect, hci::Status::kUnknownConnectionId);
    return;
  }

  FXL_DCHECK(device->connected());

  RespondWithCommandStatus(hci::kDisconnect, hci::Status::kSuccess);

  bool notify = device->connected();
  device->RemoveLink(handle);
  if (notify && !device->connected()) NotifyConnectionState(device->address(), false);

  hci::DisconnectionCompleteEventParams reply;
  reply.status = hci::Status::kSuccess;
  reply.connection_handle = params.connection_handle;
  reply.reason = hci::Status::kConnectionTerminatedByLocalHost;
  SendEvent(hci::kDisconnectionCompleteEventCode, &reply, sizeof(reply));
}

void FakeController::OnCommandPacketReceived(
    const common::PacketView<hci::CommandHeader>& command_packet) {
  hci::OpCode opcode = le16toh(command_packet.header().opcode);
  if (MaybeRespondWithDefaultStatus(opcode)) return;

  switch (opcode) {
    case hci::kReadLocalVersionInfo: {
      hci::ReadLocalVersionInfoReturnParams params;
      std::memset(&params, 0, sizeof(params));
      params.hci_version = settings_.hci_version;
      RespondWithCommandComplete(hci::kReadLocalVersionInfo, &params, sizeof(params));
      break;
    }
    case hci::kReadLocalSupportedCommands: {
      hci::ReadLocalSupportedCommandsReturnParams params;
      params.status = hci::Status::kSuccess;
      std::memcpy(params.supported_commands, settings_.supported_commands,
                  sizeof(params.supported_commands));
      RespondWithCommandComplete(hci::kReadLocalSupportedCommands, &params, sizeof(params));
      break;
    }
    case hci::kReadLocalSupportedFeatures: {
      hci::ReadLocalSupportedFeaturesReturnParams params;
      params.status = hci::Status::kSuccess;
      params.lmp_features = htole64(settings_.lmp_features_page0);
      RespondWithCommandComplete(hci::kReadLocalSupportedFeatures, &params, sizeof(params));
      break;
    }
    case hci::kReadBDADDR: {
      hci::ReadBDADDRReturnParams params;
      params.status = hci::Status::kSuccess;
      params.bd_addr = settings_.bd_addr.value();
      RespondWithCommandComplete(hci::kReadBDADDR, &params, sizeof(params));
      break;
    }
    case hci::kReadBufferSize: {
      hci::ReadBufferSizeReturnParams params;
      std::memset(&params, 0, sizeof(params));
      params.hc_acl_data_packet_length = htole16(settings_.acl_data_packet_length);
      params.hc_total_num_acl_data_packets = settings_.total_num_acl_data_packets;
      RespondWithCommandComplete(hci::kReadBufferSize, &params, sizeof(params));
      break;
    }
    case hci::kDisconnect: {
      OnDisconnectCommandReceived(command_packet.payload<hci::DisconnectCommandParams>());
      break;
    }
    case hci::kLECreateConnection: {
      OnLECreateConnectionCommandReceived(
          command_packet.payload<hci::LECreateConnectionCommandParams>());
      break;
    }
    case hci::kLECreateConnectionCancel: {
      hci::SimpleReturnParams params;
      params.status = hci::Status::kSuccess;

      if (!le_connect_pending_) {
        // No request is currently pending.
        params.status = hci::Status::kCommandDisallowed;
        RespondWithCommandComplete(hci::kLECreateConnectionCancel, &params, sizeof(params));
        return;
      }

      le_connect_pending_ = false;
      pending_le_connect_rsp_.Cancel();

      NotifyConnectionState(pending_le_connect_addr_, false, true);

      hci::LEConnectionCompleteSubeventParams response;
      std::memset(&response, 0, sizeof(response));

      response.status = hci::Status::kUnknownConnectionId;
      response.peer_address = pending_le_connect_addr_.value();
      response.peer_address_type = ToPeerAddrType(pending_le_connect_addr_.type());

      RespondWithCommandComplete(hci::kLECreateConnectionCancel, &params, sizeof(params));
      SendLEMetaEvent(hci::kLEConnectionCompleteSubeventCode, &response, sizeof(response));
      break;
    }
    case hci::kLEReadLocalSupportedFeatures: {
      hci::LEReadLocalSupportedFeaturesReturnParams params;
      params.status = hci::Status::kSuccess;
      params.le_features = htole64(settings_.le_features);
      RespondWithCommandComplete(hci::kLEReadLocalSupportedFeatures, &params, sizeof(params));
      break;
    }
    case hci::kLEReadSupportedStates: {
      hci::LEReadSupportedStatesReturnParams params;
      params.status = hci::Status::kSuccess;
      params.le_states = htole64(settings_.le_supported_states);
      RespondWithCommandComplete(hci::kLEReadSupportedStates, &params, sizeof(params));
      break;
    }
    case hci::kLEReadBufferSize: {
      hci::LEReadBufferSizeReturnParams params;
      params.status = hci::Status::kSuccess;
      params.hc_le_acl_data_packet_length = htole16(settings_.le_acl_data_packet_length);
      params.hc_total_num_le_acl_data_packets = settings_.le_total_num_acl_data_packets;
      RespondWithCommandComplete(hci::kLEReadBufferSize, &params, sizeof(params));
      break;
    }
    case hci::kSetEventMask: {
      const auto& in_params = command_packet.payload<hci::SetEventMaskCommandParams>();
      settings_.event_mask = le64toh(in_params.event_mask);

      hci::SimpleReturnParams params;
      params.status = hci::Status::kSuccess;
      RespondWithCommandComplete(hci::kSetEventMask, &params, sizeof(params));
      break;
    }
    case hci::kLESetEventMask: {
      const auto& in_params = command_packet.payload<hci::LESetEventMaskCommandParams>();
      settings_.le_event_mask = le64toh(in_params.le_event_mask);

      hci::SimpleReturnParams params;
      params.status = hci::Status::kSuccess;
      RespondWithCommandComplete(hci::kLESetEventMask, &params, sizeof(params));
      break;
    }
    case hci::kReadLocalExtendedFeatures: {
      const auto& in_params = command_packet.payload<hci::ReadLocalExtendedFeaturesCommandParams>();

      hci::ReadLocalExtendedFeaturesReturnParams out_params;
      out_params.page_number = in_params.page_number;
      out_params.maximum_page_number = 2;

      if (in_params.page_number > 2) {
        out_params.status = hci::Status::kInvalidHCICommandParameters;
      } else {
        out_params.status = hci::Status::kSuccess;

        switch (in_params.page_number) {
          case 0:
            out_params.extended_lmp_features = htole64(settings_.lmp_features_page0);
            break;
          case 1:
            out_params.extended_lmp_features = htole64(settings_.lmp_features_page1);
            break;
          case 2:
            out_params.extended_lmp_features = htole64(settings_.lmp_features_page2);
            break;
        }
      }
      RespondWithCommandComplete(hci::kReadLocalExtendedFeatures, &out_params, sizeof(out_params));
      break;
    }
    case hci::kLESetScanParameters: {
      const auto& in_params = command_packet.payload<hci::LESetScanParametersCommandParams>();

      hci::SimpleReturnParams out_params;
      if (le_scan_state_.enabled) {
        out_params.status = hci::Status::kCommandDisallowed;
      } else {
        out_params.status = hci::Status::kSuccess;
        le_scan_state_.scan_type = in_params.scan_type;
        le_scan_state_.scan_interval = le16toh(in_params.scan_interval);
        le_scan_state_.scan_window = le16toh(in_params.scan_window);
        le_scan_state_.own_address_type = in_params.own_address_type;
        le_scan_state_.filter_policy = in_params.filter_policy;
      }

      RespondWithCommandComplete(opcode, &out_params, sizeof(out_params));
      break;
    }
    case hci::kLESetScanEnable: {
      const auto& in_params = command_packet.payload<hci::LESetScanEnableCommandParams>();

      le_scan_state_.enabled = (in_params.scanning_enabled == hci::GenericEnableParam::kEnable);
      le_scan_state_.filter_duplicates =
          (in_params.filter_duplicates == hci::GenericEnableParam::kEnable);

      // Post the scan state update before scheduling the HCI Command Complete event. This
      // guarantees that single-threaded unit tests receive the scan state update BEFORE the HCI
      // command sequence terminates.
      if (scan_state_cb_) {
        FXL_DCHECK(scan_state_cb_runner_);
        scan_state_cb_runner_->PostTask(
            [ cb = scan_state_cb_, enabled = le_scan_state_.enabled ] { cb(enabled); });
      }

      hci::SimpleReturnParams out_params;
      out_params.status = hci::Status::kSuccess;
      RespondWithCommandComplete(opcode, &out_params, sizeof(out_params));

      if (le_scan_state_.enabled) SendAdvertisingReports();
      break;
    }
    case hci::kReset:
    case hci::kWriteLEHostSupport: {
      hci::SimpleReturnParams params;
      params.status = hci::Status::kSuccess;
      RespondWithCommandComplete(opcode, &params, sizeof(params));
      break;
    }
    default: {
      hci::SimpleReturnParams params;
      params.status = hci::Status::kUnknownCommand;
      RespondWithCommandComplete(opcode, &params, sizeof(params));
      break;
    }
  }
}

void FakeController::OnACLDataPacketReceived(const common::ByteBuffer& acl_data_packet) {
  // TODO(armansito): Do something here.
}

}  // namespace testing
}  // namespace bluetooth
