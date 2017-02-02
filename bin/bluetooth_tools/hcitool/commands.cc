// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <iostream>
#include <cstring>

#include "apps/bluetooth/hci/command_packet.h"
#include "apps/bluetooth/hci/event_packet.h"
#include "lib/ftl/strings/string_printf.h"

#include "command_handler_map.h"

using namespace bluetooth;

namespace hcitool {
namespace {

void LogCommandComplete(uint8_t status, hci::CommandChannel::TransactionId id) {
  std::cout << "Command Complete - status: "
            << ftl::StringPrintf("0x%02x", status) << " (id=" << id << ")"
            << std::endl;
}

constexpr size_t BufferSize(size_t payload_size) {
  return hci::CommandPacket::GetMinBufferSize(payload_size);
}

}  // namespace

void RegisterCommands(CommandHandlerMap* handler_map) {
  FTL_DCHECK(handler_map);

#define REGISTER_HANDLER(handler)                           \
  handler_map->RegisterHandler(                             \
      handler::GetCommandName(),                            \
      std::make_unique<handler>(handler_map->cmd_channel(), \
                                handler_map->task_runner()))

  REGISTER_HANDLER(ResetHandler);
  REGISTER_HANDLER(ReadBDADDRHandler);
  REGISTER_HANDLER(ReadLocalNameHandler);
  REGISTER_HANDLER(WriteLocalNameHandler);

#undef REGISTER_HANDLER
}

std::string ResetHandler::GetHelpMessage() const {
  return "reset                    Send HCI_Reset";
}

std::string ReadBDADDRHandler::GetHelpMessage() const {
  return "read-bdaddr              Send HCI_Read_BDADDR";
}

std::string ReadLocalNameHandler::GetHelpMessage() const {
  return "read-local-name          Send HCI_Read_Local_Name";
}

std::string WriteLocalNameHandler::GetHelpMessage() const {
  return "write-local-name <name>  Send HCI_Write_Local_Name";
}

bool ResetHandler::HandleCommand(
    const std::vector<std::string>& positional_args,
    size_t option_count,
    const OptionMap& options,
    const ftl::Closure& complete_cb) {
  if (positional_args.size() || option_count) {
    std::cout << "Usage: reset" << std::endl;
    return false;
  }

  auto cb = [complete_cb](hci::CommandChannel::TransactionId id,
                          const hci::EventPacket& event) {
    auto status = event.GetReturnParams<hci::ResetReturnParams>()->status;
    LogCommandComplete(status, id);
    complete_cb();
  };

  common::StaticByteBuffer<BufferSize(0u)> buffer;
  hci::CommandPacket packet(hci::kReset, &buffer);
  packet.EncodeHeader();

  auto id = cmd_channel()->SendCommand(
      packet, DefaultStatusCallback(complete_cb), cb, task_runner());

  std::cout << "Sent HCI_Reset (id=" << id << ")" << std::endl;
  return true;
}

bool ReadBDADDRHandler::HandleCommand(
    const std::vector<std::string>& positional_args,
    size_t option_count,
    const OptionMap& options,
    const ftl::Closure& complete_cb) {
  if (positional_args.size() || option_count) {
    std::cout << "Usage: read-bdaddr" << std::endl;
    return false;
  }

  auto cb = [complete_cb](hci::CommandChannel::TransactionId id,
                          const hci::EventPacket& event) {
    auto return_params = event.GetReturnParams<hci::ReadBDADDRReturnParams>();
    LogCommandComplete(return_params->status, id);
    if (return_params->status != hci::Status::kSuccess) {
      complete_cb();
      return;
    }

    std::cout << "BD_ADDR: "
              << ftl::StringPrintf("%02X", return_params->bd_addr[5]);
    for (int i = 4; i >= 0; --i)
      std::cout << ftl::StringPrintf(":%02X", return_params->bd_addr[i]);
    std::cout << std::endl;
    complete_cb();
  };

  common::StaticByteBuffer<BufferSize(0u)> buffer;
  hci::CommandPacket packet(hci::kReadBDADDR, &buffer);
  packet.EncodeHeader();

  auto id = cmd_channel()->SendCommand(
      packet, DefaultStatusCallback(complete_cb), cb, task_runner());

  std::cout << "Sent HCI_Read_BDADDR (id=" << id << ")" << std::endl;
  return true;
}

bool ReadLocalNameHandler::HandleCommand(
    const std::vector<std::string>& positional_args,
    size_t option_count,
    const OptionMap& options,
    const ftl::Closure& complete_cb) {
  if (positional_args.size() || option_count) {
    std::cout << "Usage: read-local-name" << std::endl;
    return false;
  }

  auto cb = [complete_cb](hci::CommandChannel::TransactionId id,
                          const hci::EventPacket& event) {
    auto return_params =
        event.GetReturnParams<hci::ReadLocalNameReturnParams>();
    LogCommandComplete(return_params->status, id);
    if (return_params->status != hci::Status::kSuccess) {
      complete_cb();
      return;
    }

    std::cout << "Local Name: " << return_params->local_name << std::endl;

    complete_cb();
  };

  common::StaticByteBuffer<BufferSize(0u)> buffer;
  hci::CommandPacket packet(hci::kReadLocalName, &buffer);
  packet.EncodeHeader();

  auto id = cmd_channel()->SendCommand(
      packet, DefaultStatusCallback(complete_cb), cb, task_runner());

  std::cout << "Sent HCI_Read_Local_Name (id=" << id << ")" << std::endl;
  return true;
}

bool WriteLocalNameHandler::HandleCommand(
    const std::vector<std::string>& positional_args,
    size_t option_count,
    const OptionMap& options,
    const ftl::Closure& complete_cb) {
  if (positional_args.size() != 1 || option_count) {
    std::cout << "Usage: write-local-name <name>" << std::endl;
    return false;
  }

  auto cb = [complete_cb](hci::CommandChannel::TransactionId id,
                          const hci::EventPacket& event) {
    auto return_params =
        event.GetReturnParams<hci::WriteLocalNameReturnParams>();
    LogCommandComplete(return_params->status, id);
    complete_cb();
  };

  common::StaticByteBuffer<BufferSize(hci::kMaxLocalNameLength)> buffer;
  hci::CommandPacket packet(hci::kWriteLocalName, &buffer,
                            positional_args[0].length() + 1);
  buffer.GetMutableData()[positional_args[0].length()] = '\0';
  std::strcpy(
      (char*)packet.GetPayload<hci::WriteLocalNameCommandParams>()->local_name,
      positional_args[0].c_str());
  packet.EncodeHeader();

  auto id = cmd_channel()->SendCommand(
      packet, DefaultStatusCallback(complete_cb), cb, task_runner());

  std::cout << "Sent HCI_Write_Local_Name (id=" << id << ")" << std::endl;
  return true;
}

}  // namespace hcitool
