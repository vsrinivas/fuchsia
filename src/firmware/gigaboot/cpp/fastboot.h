// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_

#include <lib/fastboot/fastboot_base.h>
#include <lib/stdcompat/span.h>

#include <fbl/vector.h>

#include "backends.h"

namespace gigaboot {

class Fastboot : public fastboot::FastbootBase {
 public:
  Fastboot(cpp20::span<uint8_t> download_buffer) : download_buffer_(download_buffer) {}
  bool IsContinue() { return continue_; }

 private:
  zx::status<> ProcessCommand(std::string_view cmd, fastboot::Transport *transport) override;
  void DoClearDownload() override;
  zx::status<void *> GetDownloadBuffer(size_t total_download_size) override;

  struct VariableCallbackEntry {
    std::string_view name;
    zx::status<> (Fastboot::*cmd)(const CommandArgs &, fastboot::Transport *);
  };

  cpp20::span<VariableCallbackEntry> GetVariableCallbackTable();

  struct CommandCallbackEntry {
    std::string_view name;
    zx::status<> (Fastboot::*cmd)(std::string_view, fastboot::Transport *);
  };

  cpp20::span<CommandCallbackEntry> GetCommandCallbackTable();

  zx::status<> GetVarMaxDownloadSize(const CommandArgs &, fastboot::Transport *);

  zx::status<> GetVar(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> Flash(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> Continue(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> DoReboot(RebootMode reboot_mode, std::string_view cmd,
                        fastboot::Transport *transport);
  zx::status<> Reboot(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> RebootBootloader(std::string_view cmd, fastboot::Transport *transport);
  zx::status<> RebootRecovery(std::string_view cmd, fastboot::Transport *transport);

  cpp20::span<uint8_t> download_buffer_;

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
