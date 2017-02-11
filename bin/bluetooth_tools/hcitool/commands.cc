// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <iostream>
#include <cstring>

#include "apps/bluetooth/hci/command_channel.h"
#include "apps/bluetooth/hci/command_packet.h"
#include "lib/ftl/strings/string_printf.h"

#include "command_dispatcher.h"

using namespace bluetooth;

using std::placeholders::_1;
using std::placeholders::_2;

namespace hcitool {
namespace {

void StatusCallback(ftl::Closure complete_cb,
                    bluetooth::hci::CommandChannel::TransactionId id,
                    bluetooth::hci::Status status) {
  std::cout << "  Command Status: " << ftl::StringPrintf("0x%02x", status)
            << " (id=" << id << ")" << std::endl;
  if (status != bluetooth::hci::Status::kSuccess)
    complete_cb();
}

hci::CommandChannel::TransactionId SendCommand(
    const CommandDispatcher& owner,
    const hci::CommandPacket& packet,
    const hci::CommandChannel::CommandCompleteCallback& cb,
    const ftl::Closure& complete_cb) {
  return owner.cmd_channel()->SendCommand(
      packet, std::bind(&StatusCallback, complete_cb, _1, _2), cb,
      owner.task_runner());
}

void LogCommandComplete(uint8_t status, hci::CommandChannel::TransactionId id) {
  std::cout << "  Command Complete - status: "
            << ftl::StringPrintf("0x%02x", status) << " (id=" << id << ")"
            << std::endl;
}

constexpr size_t BufferSize(size_t payload_size) {
  return hci::CommandPacket::GetMinBufferSize(payload_size);
}

bool HandleReset(const CommandDispatcher& owner,
                 const ftl::CommandLine& cmd_line,
                 const ftl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: reset" << std::endl;
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

  auto id = SendCommand(owner, packet, cb, complete_cb);
  std::cout << "  Sent HCI_Reset (id=" << id << ")" << std::endl;

  return true;
}

bool HandleReadBDADDR(const CommandDispatcher& owner,
                      const ftl::CommandLine& cmd_line,
                      const ftl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-bdaddr" << std::endl;
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

    std::cout << "  BD_ADDR: "
              << ftl::StringPrintf("%02X", return_params->bd_addr[5]);
    for (int i = 4; i >= 0; --i)
      std::cout << ftl::StringPrintf(":%02X", return_params->bd_addr[i]);
    std::cout << std::endl;
    complete_cb();
  };

  common::StaticByteBuffer<BufferSize(0u)> buffer;
  hci::CommandPacket packet(hci::kReadBDADDR, &buffer);
  packet.EncodeHeader();

  auto id = SendCommand(owner, packet, cb, complete_cb);
  std::cout << "  Sent HCI_Read_BDADDR (id=" << id << ")" << std::endl;

  return true;
}

bool HandleReadLocalName(const CommandDispatcher& owner,
                         const ftl::CommandLine& cmd_line,
                         const ftl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-local-name" << std::endl;
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

    std::cout << "  Local Name: " << return_params->local_name << std::endl;

    complete_cb();
  };

  common::StaticByteBuffer<BufferSize(0u)> buffer;
  hci::CommandPacket packet(hci::kReadLocalName, &buffer);
  packet.EncodeHeader();

  auto id = SendCommand(owner, packet, cb, complete_cb);
  std::cout << "  Sent HCI_Read_Local_Name (id=" << id << ")" << std::endl;

  return true;
}

bool HandleWriteLocalName(const CommandDispatcher& owner,
                          const ftl::CommandLine& cmd_line,
                          const ftl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() != 1 || cmd_line.options().size()) {
    std::cout << "  Usage: write-local-name <name>" << std::endl;
    return false;
  }

  auto cb = [complete_cb](hci::CommandChannel::TransactionId id,
                          const hci::EventPacket& event) {
    auto return_params =
        event.GetReturnParams<hci::WriteLocalNameReturnParams>();
    LogCommandComplete(return_params->status, id);
    complete_cb();
  };

  const std::string& name = cmd_line.positional_args()[0];
  common::StaticByteBuffer<BufferSize(hci::kMaxLocalNameLength)> buffer;
  hci::CommandPacket packet(hci::kWriteLocalName, &buffer, name.length() + 1);
  buffer.GetMutableData()[name.length()] = '\0';
  std::strcpy(
      (char*)packet.GetPayload<hci::WriteLocalNameCommandParams>()->local_name,
      name.c_str());
  packet.EncodeHeader();

  auto id = SendCommand(owner, packet, cb, complete_cb);
  std::cout << "  Sent HCI_Write_Local_Name (id=" << id << ")" << std::endl;

  return true;
}

}  // namespace

void RegisterCommands(CommandDispatcher* handler_map) {
  FTL_DCHECK(handler_map);

  handler_map->RegisterHandler("reset", "Send HCI_Reset", HandleReset);
  handler_map->RegisterHandler("read-bdaddr", "Send HCI_Read_BDADDR",
                               HandleReadBDADDR);
  handler_map->RegisterHandler("read-local-name", "Send HCI_Read_Local_Name",
                               HandleReadLocalName);
  handler_map->RegisterHandler("write-local-name", "Send HCI_Write_Local_Name",
                               HandleWriteLocalName);
}

}  // namespace hcitool
