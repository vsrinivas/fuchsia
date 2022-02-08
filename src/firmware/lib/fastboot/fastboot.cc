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
  kData,
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
  } else if (resp_type == ResponseType::kData) {
    type = "DATA";
  } else {
    FX_LOGF(ERROR, kFastbootLogTag, "Invalid response type %d\n", static_cast<int>(resp_type));
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  std::string resp = type + message;
  if (zx::status<> ret = transport->Send(resp); ret.is_error()) {
    FX_LOGF(ERROR, kFastbootLogTag, "Failed to write packet %d\n", ret.status_value());
    return zx::error(ret.status_value());
  }

  return ret_status;
}

zx::status<> SendDataResponse(size_t data_size, Transport* transport) {
  std::string message = fxl::StringPrintf("%08zx", data_size);
  return SendResponse(ResponseType::kData, message, transport);
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
      {
          .name = "download",
          .cmd = &Fastboot::Download,
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
    size_t packet_size = transport->PeekPacketSize();
    if (packet_size > remaining_download_) {
      ClearDownload();
      return SendResponse(ResponseType::kFail, "Unexpected amount of download", transport);
    }

    size_t total_size = download_vmo_mapper_.size();
    size_t offset = total_size - remaining_download_;
    uint8_t* start = static_cast<uint8_t*>(download_vmo_mapper_.start());
    zx::status<size_t> ret = transport->ReceivePacket(start + offset, remaining_download_);
    if (ret.is_error()) {
      ClearDownload();
      return SendResponse(ResponseType::kFail, "Failed to write to vmo", transport,
                          zx::error(ret.status_value()));
    }

    remaining_download_ -= ret.value();
    if (remaining_download_ == 0) {
      state_ = State::kCommand;
      return SendResponse(ResponseType::kOkay, "", transport);
    }

    return zx::ok();
  }

  return zx::ok();
}

void Fastboot::ClearDownload() {
  state_ = State::kCommand;
  download_vmo_mapper_.Reset();
  remaining_download_ = 0;
}

zx::status<> Fastboot::Download(const std::string& command, Transport* transport) {
  ClearDownload();
  std::vector<std::string_view> args =
      fxl::SplitString(command, ":", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (args.size() < 2) {
    return SendResponse(ResponseType::kFail, "Not enough argument", transport);
  }

  remaining_download_ = static_cast<size_t>(std::stoul(args[1].data(), nullptr, 16));
  if (remaining_download_ == 0) {
    return SendResponse(ResponseType::kFail, "Empty size download is not allowed", transport);
  }

  if (zx_status_t ret = download_vmo_mapper_.CreateAndMap(remaining_download_, "fastboot download");
      ret != ZX_OK) {
    ClearDownload();
    return SendResponse(ResponseType::kFail, "Failed to create download vmo", transport,
                        zx::error(ZX_ERR_INTERNAL));
  }

  state_ = State::kDownload;
  return SendDataResponse(remaining_download_, transport);
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
