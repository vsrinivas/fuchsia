// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_controller.h"

#include <endian.h>

#include "apps/bluetooth/lib/hci/command_packet.h"
#include "apps/bluetooth/lib/hci/event_packet.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "lib/ftl/strings/string_printf.h"

namespace bluetooth {
namespace testing {
namespace {

template <typename NUM_TYPE, typename ENUM_TYPE>
void SetBit(NUM_TYPE* num_type, ENUM_TYPE bit) {
  *num_type |= static_cast<NUM_TYPE>(bit);
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

FakeController::FakeController(mx::channel cmd_channel, mx::channel acl_data_channel)
    : FakeControllerBase(std::move(cmd_channel), std::move(acl_data_channel)) {}

FakeController::~FakeController() {
  if (IsStarted()) Stop();
}

void FakeController::SetDefaultResponseStatus(hci::OpCode opcode, hci::Status status) {
  FTL_DCHECK(status != hci::Status::kSuccess);
  default_status_map_[opcode] = status;
}

void FakeController::ClearDefaultResponseStatus(hci::OpCode opcode) {
  default_status_map_.erase(opcode);
}

void FakeController::RespondWithCommandComplete(hci::OpCode opcode, void* return_params,
                                                uint8_t return_params_size) {
  // Either both are zero or neither is.
  FTL_DCHECK(!!return_params == !!return_params_size);

  common::DynamicByteBuffer buffer(hci::EventPacket::GetMinBufferSize(
      sizeof(hci::CommandCompleteEventParams) + return_params_size));
  hci::MutableEventPacket event_packet(hci::kCommandCompleteEventCode, &buffer);
  auto payload = event_packet.GetMutablePayload<hci::CommandCompleteEventParams>();
  payload->num_hci_command_packets = settings_.num_hci_command_packets;
  payload->command_opcode = htole16(opcode);
  std::memcpy(payload->return_parameters, return_params, return_params_size);

  SendCommandChannelPacket(buffer);
}

bool FakeController::MaybeRespondWithDefaultStatus(hci::OpCode opcode) {
  auto iter = default_status_map_.find(opcode);
  if (iter == default_status_map_.end()) return false;

  FTL_LOG(INFO) << ftl::StringPrintf(
      "hci: FakeController: Responding with error (command: 0x%04x, status: 0x%02x", opcode,
      iter->second);

  hci::SimpleReturnParams params;
  params.status = iter->second;
  RespondWithCommandComplete(opcode, &params, sizeof(params));
  return true;
}

void FakeController::OnCommandPacketReceived(const hci::CommandPacket& command_packet) {
  if (MaybeRespondWithDefaultStatus(command_packet.opcode())) return;

  switch (command_packet.opcode()) {
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
      auto in_params = command_packet.GetPayload<hci::SetEventMaskCommandParams>();
      settings_.event_mask = le64toh(in_params->event_mask);

      hci::SimpleReturnParams params;
      params.status = hci::Status::kSuccess;
      RespondWithCommandComplete(hci::kSetEventMask, &params, sizeof(params));
      break;
    }
    case hci::kLESetEventMask: {
      auto in_params = command_packet.GetPayload<hci::LESetEventMaskCommandParams>();
      settings_.le_event_mask = le64toh(in_params->le_event_mask);

      hci::SimpleReturnParams params;
      params.status = hci::Status::kSuccess;
      RespondWithCommandComplete(hci::kLESetEventMask, &params, sizeof(params));
      break;
    }
    case hci::kReadLocalExtendedFeatures: {
      auto in_params = command_packet.GetPayload<hci::ReadLocalExtendedFeaturesCommandParams>();

      hci::ReadLocalExtendedFeaturesReturnParams out_params;
      out_params.page_number = in_params->page_number;
      out_params.maximum_page_number = 2;

      if (in_params->page_number > 2) {
        out_params.status = hci::Status::kInvalidHCICommandParameters;
      } else {
        out_params.status = hci::Status::kSuccess;

        switch (in_params->page_number) {
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
    case hci::kLESetScanEnable:
    case hci::kReset:
    case hci::kLESetScanParameters:
    case hci::kWriteLEHostSupport: {
      hci::SimpleReturnParams params;
      params.status = hci::Status::kSuccess;
      RespondWithCommandComplete(command_packet.opcode(), &params, sizeof(params));
      break;
    }
    default: {
      hci::SimpleReturnParams params;
      params.status = hci::Status::kUnknownCommand;
      RespondWithCommandComplete(command_packet.opcode(), &params, sizeof(params));
      break;
    }
  }
}

void FakeController::OnACLDataPacketReceived(const common::ByteBuffer& acl_data_packet) {
  // TODO(armansito): Do something here.
}

}  // namespace testing
}  // namespace bluetooth
