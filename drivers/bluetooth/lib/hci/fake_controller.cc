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
namespace hci {
namespace test {
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
  hci_version = HCIVersion::k5_0;
  num_hci_command_packets = 1;
}

void FakeController::Settings::ApplyLEOnlyConfig() {
  ApplyDefaults();

  le_acl_data_packet_length = 512;
  le_total_num_acl_data_packets = 1;

  SetBit(&lmp_features_page0, LMPFeature::kBREDRNotSupported);
  SetBit(&lmp_features_page0, LMPFeature::kLESupported);
  SetBit(&lmp_features_page0, LMPFeature::kExtendedFeatures);

  // TODO(armansito): Set more feature bits as we support them.

  SetBit(supported_commands, SupportedCommand::kDisconnect);
  SetBit(supported_commands + 5, SupportedCommand::kSetEventMask);
  SetBit(supported_commands + 5, SupportedCommand::kReset);
  SetBit(supported_commands + 14, SupportedCommand::kReadLocalVersionInformation);
  SetBit(supported_commands + 14, SupportedCommand::kReadLocalSupportedFeatures);
  SetBit(supported_commands + 14, SupportedCommand::kReadLocalExtendedFeatures);
  SetBit(supported_commands + 24, SupportedCommand::kWriteLEHostSupport);
  SetBit(supported_commands + 25, SupportedCommand::kLESetEventMask);
  SetBit(supported_commands + 25, SupportedCommand::kLEReadBufferSize);
  SetBit(supported_commands + 25, SupportedCommand::kLEReadLocalSupportedFeatures);

  // TODO(armansito): Set more command bits as we support them.
}

FakeController::FakeController(const Settings& settings, mx::channel cmd_channel,
                               mx::channel acl_data_channel)
    : FakeControllerBase(std::move(cmd_channel), std::move(acl_data_channel)),
      settings_(settings) {}

FakeController::~FakeController() {
  if (IsStarted()) Stop();
}

void FakeController::SetDefaultResponseStatus(OpCode opcode, Status status) {
  FTL_DCHECK(status != Status::kSuccess);
  default_status_map_[opcode] = status;
}

void FakeController::ClearDefaultResponseStatus(OpCode opcode) {
  default_status_map_.erase(opcode);
}

void FakeController::RespondWithCommandComplete(OpCode opcode, void* return_params,
                                                uint8_t return_params_size) {
  // Either both are zero or neither is.
  FTL_DCHECK(!!return_params == !!return_params_size);

  common::DynamicByteBuffer buffer(
      EventPacket::GetMinBufferSize(sizeof(CommandCompleteEventParams) + return_params_size));
  MutableEventPacket event_packet(kCommandCompleteEventCode, &buffer);
  auto payload = event_packet.GetMutablePayload<CommandCompleteEventParams>();
  payload->num_hci_command_packets = settings_.num_hci_command_packets;
  payload->command_opcode = htole16(opcode);
  std::memcpy(payload->return_parameters, return_params, return_params_size);

  SendCommandChannelPacket(buffer);
}

bool FakeController::MaybeRespondWithDefaultStatus(OpCode opcode) {
  auto iter = default_status_map_.find(opcode);
  if (iter == default_status_map_.end()) return false;

  FTL_LOG(INFO) << ftl::StringPrintf(
      "hci: FakeController: Responding with error (command: 0x%04x, status: 0x%02x", opcode,
      iter->second);

  SimpleReturnParams params;
  params.status = iter->second;
  RespondWithCommandComplete(opcode, &params, sizeof(params));
  return true;
}

void FakeController::OnCommandPacketReceived(const CommandPacket& command_packet) {
  if (MaybeRespondWithDefaultStatus(command_packet.opcode())) return;

  switch (command_packet.opcode()) {
    case kReadLocalVersionInfo: {
      ReadLocalVersionInfoReturnParams params;
      std::memset(&params, 0, sizeof(params));
      params.hci_version = settings_.hci_version;
      RespondWithCommandComplete(kReadLocalVersionInfo, &params, sizeof(params));
      break;
    }
    case kReadLocalSupportedCommands: {
      ReadLocalSupportedCommandsReturnParams params;
      params.status = Status::kSuccess;
      std::memcpy(params.supported_commands, settings_.supported_commands,
                  sizeof(params.supported_commands));
      RespondWithCommandComplete(kReadLocalSupportedCommands, &params, sizeof(params));
      break;
    }
    case kReadLocalSupportedFeatures: {
      ReadLocalSupportedFeaturesReturnParams params;
      params.status = Status::kSuccess;
      params.lmp_features = htole64(settings_.lmp_features_page0);
      RespondWithCommandComplete(kReadLocalSupportedFeatures, &params, sizeof(params));
      break;
    }
    case kReadBDADDR: {
      ReadBDADDRReturnParams params;
      params.status = Status::kSuccess;
      params.bd_addr = settings_.bd_addr;
      RespondWithCommandComplete(kReadBDADDR, &params, sizeof(params));
      break;
    }
    case kReadBufferSize: {
      ReadBufferSizeReturnParams params;
      std::memset(&params, 0, sizeof(params));
      params.hc_acl_data_packet_length = htole16(settings_.acl_data_packet_length);
      params.hc_total_num_acl_data_packets = settings_.total_num_acl_data_packets;
      RespondWithCommandComplete(kReadBufferSize, &params, sizeof(params));
      break;
    }
    case kLEReadLocalSupportedFeatures: {
      LEReadLocalSupportedFeaturesReturnParams params;
      params.status = Status::kSuccess;
      params.le_features = htole64(settings_.le_features);
      RespondWithCommandComplete(kLEReadLocalSupportedFeatures, &params, sizeof(params));
      break;
    }
    case kLEReadSupportedStates: {
      LEReadSupportedStatesReturnParams params;
      params.status = Status::kSuccess;
      params.le_states = htole64(settings_.le_supported_states);
      RespondWithCommandComplete(kLEReadSupportedStates, &params, sizeof(params));
      break;
    }
    case kLEReadBufferSize: {
      LEReadBufferSizeReturnParams params;
      params.status = Status::kSuccess;
      params.hc_le_acl_data_packet_length = htole16(settings_.le_acl_data_packet_length);
      params.hc_total_num_le_acl_data_packets = settings_.le_total_num_acl_data_packets;
      RespondWithCommandComplete(kLEReadBufferSize, &params, sizeof(params));
      break;
    }
    case kSetEventMask: {
      auto in_params = command_packet.GetPayload<SetEventMaskCommandParams>();
      settings_.event_mask = le64toh(in_params->event_mask);

      SimpleReturnParams params;
      params.status = Status::kSuccess;
      RespondWithCommandComplete(kSetEventMask, &params, sizeof(params));
      break;
    }
    case kLESetEventMask: {
      auto in_params = command_packet.GetPayload<LESetEventMaskCommandParams>();
      settings_.le_event_mask = le64toh(in_params->le_event_mask);

      SimpleReturnParams params;
      params.status = Status::kSuccess;
      RespondWithCommandComplete(kLESetEventMask, &params, sizeof(params));
      break;
    }
    case kReadLocalExtendedFeatures: {
      auto in_params = command_packet.GetPayload<ReadLocalExtendedFeaturesCommandParams>();

      ReadLocalExtendedFeaturesReturnParams out_params;
      out_params.page_number = in_params->page_number;
      out_params.maximum_page_number = 2;

      if (in_params->page_number > 2) {
        out_params.status = Status::kInvalidHCICommandParameters;
      } else {
        out_params.status = Status::kSuccess;

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
      RespondWithCommandComplete(kReadLocalExtendedFeatures, &out_params, sizeof(out_params));
      break;
    }
    case kReset:
    case kWriteLEHostSupport: {
      SimpleReturnParams params;
      params.status = Status::kSuccess;
      RespondWithCommandComplete(command_packet.opcode(), &params, sizeof(params));
      break;
    }
    default: {
      SimpleReturnParams params;
      params.status = Status::kUnknownCommand;
      RespondWithCommandComplete(command_packet.opcode(), &params, sizeof(params));
      break;
    }
  }
}

void FakeController::OnACLDataPacketReceived(const common::ByteBuffer& acl_data_packet) {
  // TODO(armansito): Do something here.
}

}  // namespace test
}  // namesapce hci
}  // namespace bluetooth
