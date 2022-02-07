// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fastboot/fastboot.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include <string_view>
#include <vector>

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace fastboot {
namespace {

constexpr char kFastbootLogTag[] = __FILE__;
constexpr char kOemPrefix[] = "oem ";

enum class ResponseType {
  kOkay,
  kInfo,
  kFail,
};

zx::status<> SendResponse(ResponseType resp_type, const std::string& message, Transport* transport,
                          zx::status<> ret_status = zx::ok()) {
  const char* type = nullptr;
  if (resp_type == ResponseType::kOkay) {
    type = "OKAY";
  } else if (resp_type == ResponseType::kInfo) {
    type = "INFO";
  } else if (resp_type == ResponseType::kFail) {
    type = "FAIL";
  } else {
    FX_LOGF(ERROR, kFastbootLogTag, "Invalid response type %d\n", static_cast<int>(resp_type));
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  std::string resp = type + message;
  if (zx::status<> ret = transport->Send(resp); ret.is_error()) {
    FX_LOGF(ERROR, kFastbootLogTag, "Failed to write packet %d\n", ret.status_value());
    return zx::error(ZX_ERR_IO);
  }

  return ret_status;
}

bool MatchCommand(const std::string_view cmd, std::string_view ref) {
  if (cmd.compare(0, strlen(kOemPrefix), kOemPrefix) == 0) {
    // TODO(217597389): Once we need to support "oem " commands, figure out
    // how to do the command matching. For now return false whenever we see
    // an oem command.
    return false;
  } else {
    // find the first occurrence of ":". if there isn't, return value will be
    // string::npos, which will lead to full string comparison.
    size_t pos = cmd.find(":");
    return cmd.compare(0, pos, ref, 0, ref.size()) == 0;
  }
}

}  // namespace

const std::vector<Fastboot::CommandEntry> Fastboot::GetCommandTable() {
  // Using a static pointer and allocate with `new` so that the static instance
  // never gets deleted.
  static const std::vector<CommandEntry>* kCommandTable = new std::vector<CommandEntry>({
      {
          .name = "getvar",
          .cmd = &Fastboot::GetVar,
      },
  });
  return *kCommandTable;
}

const Fastboot::VariableHashTable& Fastboot::GetVariableTable() {
  // Using a static pointer and allocate with `new` so that the static instance
  // never gets deleted.
  static const VariableHashTable* kVariableTable = new VariableHashTable({
      {"max-download-size", &Fastboot::GetVarMaxDownloadSize},
  });
  return *kVariableTable;
}

Fastboot::Fastboot(size_t max_download_size) : max_download_size_(max_download_size) {}

zx::status<> Fastboot::ProcessPacket(Transport* transport) {
  if (!transport->PeekPacketSize()) {
    return zx::ok();
  }

  if (state_ == State::kCommand) {
    std::string command(transport->PeekPacketSize(), '\0');
    zx::status<size_t> ret = transport->ReceivePacket(command.data(), command.size());
    if (!ret.is_ok()) {
      return SendResponse(ResponseType::kFail, "Fail to read command", transport,
                          zx::error(ret.status_value()));
    }

    for (const CommandEntry& cmd : GetCommandTable()) {
      if (MatchCommand(command, cmd.name)) {
        return (this->*cmd.cmd)(command, transport);
      }
    }

    return SendResponse(ResponseType::kFail, "Unsupported command", transport);
  } else if (state_ == State::kDownload) {
    // TODO(217597389): To implement
  }

  return zx::ok();
}

zx::status<> Fastboot::GetVar(const std::string& command, Transport* transport) {
  std::vector<std::string_view> args =
      fxl::SplitString(command, ":", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (args.size() < 2) {
    return SendResponse(ResponseType::kFail, "Not enough arguments", transport);
  }

  const VariableHashTable& var_table = GetVariableTable();
  const VariableHashTable::const_iterator find_res = var_table.find(args[1].data());
  if (find_res == var_table.end()) {
    return SendResponse(ResponseType::kFail, "Unknown variable", transport);
  }

  zx::status<std::string> var_ret = (this->*(find_res->second))(args, transport);
  if (var_ret.is_error()) {
    return SendResponse(ResponseType::kFail, "Fail to get variable", transport,
                        zx::error(var_ret.status_value()));
  }

  return SendResponse(ResponseType::kOkay, var_ret.value(), transport);
}

zx::status<std::string> Fastboot::GetVarMaxDownloadSize(const std::vector<std::string_view>&,
                                                        Transport*) {
  return zx::ok(fxl::StringPrintf("0x%08zx", max_download_size_));
}

}  // namespace fastboot
