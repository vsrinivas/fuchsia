// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_

#include <lib/abr/ops.h>
#include <lib/fastboot/fastboot_base.h>
#include <lib/stdcompat/span.h>
#include <lib/zircon_boot/zircon_boot.h>

#include <optional>
#include <string_view>
#include <variant>

#include <fbl/vector.h>

#include "backends.h"

namespace gigaboot {

class Fastboot : public fastboot::FastbootBase {
 public:
  Fastboot(cpp20::span<uint8_t> download_buffer, ZirconBootOps zb_ops)
      : download_buffer_(download_buffer), zb_ops_(zb_ops) {}
  bool IsContinue() { return continue_; }

 private:
  zx::status<> ProcessCommand(std::string_view cmd, fastboot::Transport *transport) override;
  void DoClearDownload() override;
  zx::status<void *> GetDownloadBuffer(size_t total_download_size) override;
  AbrOps GetAbrOps() { return GetAbrOpsFromZirconBootOps(&zb_ops_); }

  // A function to call to determine the value of a variable.
  // Variables with constant, i.e. compile-time values should instead
  // define their value via the string variant.
  using VarFunc = zx::status<> (Fastboot::*)(const CommandArgs &, fastboot::Transport *);

  struct VariableEntry {
    std::string_view name;
    std::variant<VarFunc, std::string_view> var;
  };

  cpp20::span<VariableEntry> GetVariableTable();

  struct CommandCallbackEntry {
    std::string_view name;
    zx::status<> (Fastboot::*cmd)(std::string_view, fastboot::Transport *);
  };

  cpp20::span<CommandCallbackEntry> GetCommandCallbackTable();

  zx::status<> GetVarMaxDownloadSize(const CommandArgs &, fastboot::Transport *);
  zx::status<> GetVarCurrentSlot(const CommandArgs &, fastboot::Transport *);
  zx::status<> GetVarSlotLastSetActive(const CommandArgs &, fastboot::Transport *);
  zx::status<> GetVarSlotRetryCount(const CommandArgs &, fastboot::Transport *);
  zx::status<> GetVarSlotSuccessful(const CommandArgs &, fastboot::Transport *);
  zx::status<> GetVarSlotUnbootable(const CommandArgs &, fastboot::Transport *);

  zx::status<> GetVar(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> Flash(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> Continue(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> DoReboot(RebootMode reboot_mode, std::string_view cmd,
                        fastboot::Transport *transport);
  zx::status<> Reboot(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> RebootBootloader(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> RebootRecovery(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> SetActive(std::string_view cmd, fastboot::Transport *transport);

  cpp20::span<uint8_t> download_buffer_;
  ZirconBootOps zb_ops_;
  bool continue_ = false;
};

// APIs for fastboot over tcp.

class TcpTransportInterface {
 public:
  // Interface for reading from/writing to a tcp connection. Implementation should
  // guarantee that these operations are blocking.
  virtual bool Read(void *out, size_t size) = 0;
  virtual bool Write(const void *data, size_t size) = 0;
};

constexpr size_t kFastbootHandshakeMessageLength = 4;
constexpr size_t kFastbootTcpLengthPrefixBytes = 8;

// Run a fastboot session after tcp connection is established.
void FastbootTcpSession(TcpTransportInterface &interface, Fastboot &fastboot);

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_
