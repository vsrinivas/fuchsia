// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_

#include <lib/fastboot/fastboot_base.h>
#include <lib/stdcompat/span.h>

namespace gigaboot {

class Fastboot : public fastboot::FastbootBase {
 public:
  bool IsContinue() { return continue_; }

 private:
  zx::status<> ProcessCommand(std::string_view cmd, fastboot::Transport *transport) override;
  void DoClearDownload() override;
  zx::status<void *> GetDownloadBuffer(size_t total_download_size) override;

  struct CommandCallbackEntry {
    std::string_view name;
    zx::status<> (Fastboot::*cmd)(std::string_view, fastboot::Transport *);
  };

  cpp20::span<CommandCallbackEntry> GetCommandCallbackTable();

  zx::status<> Continue(std::string_view cmd, fastboot::Transport *transport);

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
