// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fastboot.h"

#include <ctype.h>
#include <lib/abr/abr.h>
#include <lib/fastboot/fastboot_base.h>
#include <lib/zircon_boot/zircon_boot.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#include <phys/efi/main.h>

#include "utils.h"

namespace gigaboot {

zx::status<> Fastboot::ProcessCommand(std::string_view cmd, fastboot::Transport *transport) {
  auto cmd_table = GetCommandCallbackTable();
  for (const CommandCallbackEntry &ele : cmd_table) {
    if (MatchCommand(cmd, ele.name.data())) {
      return (this->*(ele.cmd))(cmd, transport);
    }
  }
  return SendResponse(ResponseType::kFail, "Unsupported command", transport);
}

void Fastboot::DoClearDownload() {}

zx::status<void *> Fastboot::GetDownloadBuffer(size_t total_download_size) {
  return zx::ok(download_buffer_.data());
}

cpp20::span<Fastboot::VariableCallbackEntry> Fastboot::GetVariableCallbackTable() {
  static VariableCallbackEntry var_entries[] = {
      {"max-download-size", &Fastboot::GetVarMaxDownloadSize},
      {"current-slot", &Fastboot::GetVarCurrentSlot},
  };

  return var_entries;
}

cpp20::span<Fastboot::CommandCallbackEntry> Fastboot::GetCommandCallbackTable() {
  static CommandCallbackEntry cmd_entries[] = {
      {"getvar", &Fastboot::GetVar},
      {"flash", &Fastboot::Flash},
      {"continue", &Fastboot::Continue},
      {"reboot", &Fastboot::Reboot},
      {"reboot-bootloader", &Fastboot::RebootBootloader},
      {"reboot-recovery", &Fastboot::RebootRecovery},
      {"set_active", &Fastboot::SetActive},
  };

  return cmd_entries;
}

zx::status<> Fastboot::Reboot(std::string_view cmd, fastboot::Transport *transport) {
  return DoReboot(RebootMode::kNormal, cmd, transport);
}

zx::status<> Fastboot::RebootBootloader(std::string_view cmd, fastboot::Transport *transport) {
  return DoReboot(RebootMode::kBootloader, cmd, transport);
}

zx::status<> Fastboot::RebootRecovery(std::string_view cmd, fastboot::Transport *transport) {
  return DoReboot(RebootMode::kRecovery, cmd, transport);
}

zx::status<> Fastboot::DoReboot(RebootMode reboot_mode, std::string_view cmd,
                                fastboot::Transport *transport) {
  if (!SetRebootMode(reboot_mode)) {
    return SendResponse(ResponseType::kFail, "Failed to set reboot mode", transport,
                        zx::error(ZX_ERR_INTERNAL));
  }

  // ResetSystem() below won't return. Thus sends a OKAY response first in case we succeed.
  zx::status<> res = SendResponse(ResponseType::kOkay, "", transport);
  if (res.is_error()) {
    return res;
  }

  efi_status status =
      gEfiSystemTable->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
  if (status != EFI_SUCCESS) {
    printf("Failed to reboot: %s\n", EfiStatusToString(status));
    return zx::error(ZX_ERR_INTERNAL);
  }

  return zx::ok();
}

zx::status<> Fastboot::SetActive(std::string_view cmd, fastboot::Transport *transport) {
  CommandArgs args;
  ExtractCommandArgs(cmd, ":", args);

  if (args.num_args < 2) {
    return SendResponse(ResponseType::kFail, "missing slot name", transport);
  }

  // It is not valid to explicitly set the slot to recovery.
  AbrSlotIndex idx = kAbrSlotIndexR;
  if (args.args[1] == "a") {
    idx = kAbrSlotIndexA;
  } else if (args.args[1] == "b") {
    idx = kAbrSlotIndexB;
  } else {
    return SendResponse(ResponseType::kFail, "slot name is invalid", transport);
  }

  AbrOps abr_ops = GetAbrOps();
  AbrResult res = AbrMarkSlotActive(&abr_ops, idx);
  if (res != kAbrResultOk) {
    return SendResponse(ResponseType::kFail, "Failed to set slot", transport,
                        zx::error(ZX_ERR_INTERNAL));
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

zx::status<> Fastboot::GetVar(std::string_view cmd, fastboot::Transport *transport) {
  CommandArgs args;
  ExtractCommandArgs(cmd, ":", args);
  if (args.num_args < 2) {
    return SendResponse(ResponseType::kFail, "Not enough arguments", transport);
  }

  auto var_table = GetVariableCallbackTable();
  for (const VariableCallbackEntry &ele : var_table) {
    if (args.args[1] == ele.name) {
      return (this->*(ele.cmd))(args, transport);
    }
  }

  return SendResponse(ResponseType::kFail, "Unknown variable", transport);
}

zx::status<> Fastboot::GetVarMaxDownloadSize(const CommandArgs &, fastboot::Transport *transport) {
  char size_str[16] = {0};
  snprintf(size_str, sizeof(size_str), "0x%08zx", download_buffer_.size());
  return SendResponse(ResponseType::kOkay, size_str, transport);
}

zx::status<> Fastboot::GetVarCurrentSlot(const CommandArgs &, fastboot::Transport *transport) {
  AbrOps abr_ops = GetAbrOps();

  char const *slot_str;
  AbrSlotIndex slot = AbrGetBootSlot(&abr_ops, false, nullptr);
  switch (slot) {
    case kAbrSlotIndexA:
      slot_str = "a";
      break;
    case kAbrSlotIndexB:
      slot_str = "b";
      break;
    case kAbrSlotIndexR:
      slot_str = "r";
      break;
    default:
      slot_str = "";
      break;
  }

  return SendResponse(ResponseType::kOkay, slot_str, transport);
}

zx::status<> Fastboot::Flash(std::string_view cmd, fastboot::Transport *transport) {
  CommandArgs args;
  ExtractCommandArgs(cmd, ":", args);
  if (args.num_args < 2) {
    return SendResponse(ResponseType::kFail, "Not enough argument", transport);
  }

  ZX_ASSERT(args.args[1].size() < fastboot::kMaxCommandPacketSize);

  size_t write_size;
  bool res =
      zb_ops_.write_to_partition(&zb_ops_, std::string(args.args[1]).data(), 0,
                                 total_download_size(), download_buffer_.data(), &write_size);

  if (!res || write_size != total_download_size()) {
    return SendResponse(ResponseType::kFail, "Failed to write to partition", transport,
                        zx::error(ZX_ERR_INTERNAL));
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

zx::status<> Fastboot::Continue(std::string_view cmd, fastboot::Transport *transport) {
  continue_ = true;
  return SendResponse(ResponseType::kOkay, "", transport);
}

// The transport implementation for a TCP fastboot packet.
class PacketTransport : public fastboot::Transport {
 public:
  PacketTransport(TcpTransportInterface &interface, size_t packet_size)
      : interface_(&interface), packet_size_(packet_size) {}

  zx::status<size_t> ReceivePacket(void *dst, size_t capacity) override {
    if (packet_size_ > capacity) {
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    }

    if (!interface_->Read(dst, packet_size_)) {
      return zx::error(ZX_ERR_INTERNAL);
    }

    return zx::ok(packet_size_);
  }

  // Peek the size of the next packet.
  size_t PeekPacketSize() override { return packet_size_; }

  zx::status<> Send(std::string_view packet) override {
    // Prepend a length prefix.
    size_t size = packet.size();
    uint64_t be_size = ToBigEndian(size);
    if (!interface_->Write(&be_size, sizeof(be_size))) {
      return zx::error(ZX_ERR_INTERNAL);
    }

    if (!interface_->Write(packet.data(), size)) {
      return zx::error(ZX_ERR_INTERNAL);
    }

    return zx::ok();
  }

 private:
  TcpTransportInterface *interface_ = nullptr;
  size_t packet_size_ = 0;
};

void FastbootTcpSession(TcpTransportInterface &interface, Fastboot &fastboot) {
  // Whatever we receive, sends a handshake message first to improve performance.
  if (!interface.Write("FB01", 4)) {
    printf("Failed to write handshake message\n");
    return;
  }

  char handshake_buffer[kFastbootHandshakeMessageLength + 1] = {0};
  if (!interface.Read(handshake_buffer, kFastbootHandshakeMessageLength)) {
    printf("Failed to read handshake message\n");
    return;
  }

  // We expect "FBxx", where xx is a numeric value
  if (strncmp(handshake_buffer, "FB", 2) != 0 || !isdigit(handshake_buffer[2]) ||
      !isdigit(handshake_buffer[3])) {
    printf("Invalid handshake message %s\n", handshake_buffer);
    return;
  }

  while (true) {
    // Each fastboot packet is a length-prefixed data sequence. Read the length
    // prefix first.
    uint64_t packet_length = 0;
    if (!interface.Read(&packet_length, sizeof(packet_length))) {
      printf("Failed to read length prefix. Remote client might be disconnected\n");
      return;
    }

    // Process the length prefix. Convert big-endian to integer.
    packet_length = BigToHostEndian(packet_length);

    // Construct and pass a packet transport to fastboot.
    PacketTransport packet(interface, packet_length);
    zx::status<> ret = fastboot.ProcessPacket(&packet);
    if (ret.is_error()) {
      printf("Failed to process fastboot packet\n");
      return;
    }

    if (fastboot.IsContinue()) {
      printf("Resuming boot...\n");
      return;
    }
  }
}

}  // namespace gigaboot
