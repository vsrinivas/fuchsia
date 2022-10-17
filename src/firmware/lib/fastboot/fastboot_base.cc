// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fastboot/fastboot_base.h"

#include <assert.h>
#include <stdio.h>

#include <string>

namespace fastboot {

bool FastbootBase::MatchCommand(std::string_view cmd, std::string_view ref) {
  if (cmd.compare(0, strlen(kOemPrefix), kOemPrefix) == 0) {
    // For oem commands, we require that arguments are separated by spaces. The first argument after
    // oem specifies the command type. `ref` should look like "oem <command name>".
    return cmd.compare(0, cmd.find(" ", sizeof(kOemPrefix)), ref, 0, ref.size()) == 0;
  } else {
    // find the first occurrence of ":". if there isn't, return value will be
    // string::npos, which will lead to full string comparison.
    size_t pos = cmd.find(":");
    return cmd.compare(0, pos, ref, 0, ref.size()) == 0;
  }
}

zx::result<> FastbootBase::SendResponse(ResponseType resp_type, std::string_view message,
                                        Transport* transport, zx::result<> status_code) {
  const char* type = nullptr;
  if (resp_type == ResponseType::kOkay) {
    type = "OKAY";
  } else if (resp_type == ResponseType::kInfo) {
    type = "INFO";
  } else if (resp_type == ResponseType::kFail) {
    type = "FAIL";
  } else {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  char resp_buffer[kMaxCommandPacketSize + 1] = {0};
  if (status_code.is_ok()) {
    snprintf(resp_buffer, sizeof(resp_buffer), "%s%s", type, message.data());
  } else {
#if defined(__Fuchsia__)
    snprintf(resp_buffer, sizeof(resp_buffer), "%s%s(%s)", type, message.data(),
             status_code.status_string());
#else
    snprintf(resp_buffer, sizeof(resp_buffer), "%s%s(%d)", type, message.data(),
             status_code.error_value());
#endif
  }

  if (zx::result<> ret = transport->Send(std::string_view{resp_buffer, strlen(resp_buffer)});
      ret.is_error()) {
    return zx::error(ret.status_value());
  }

  return status_code;
}

zx::result<> FastbootBase::SendDataResponse(size_t data_size, Transport* transport) {
  char resp_buffer[kMaxCommandPacketSize + 1] = {0};
  snprintf(resp_buffer, sizeof(resp_buffer), "DATA%08zx", data_size);
  return transport->Send(std::string_view{resp_buffer, strlen(resp_buffer)});
}

zx::result<> FastbootBase::ProcessPacket(Transport* transport) {
  if (!transport->PeekPacketSize()) {
    return zx::ok();
  }

  if (state_ == State::kCommand) {
    char command[kMaxCommandPacketSize + 1] = {0};
    zx::result<size_t> ret = transport->ReceivePacket(command, sizeof(command));
    if (!ret.is_ok()) {
      return SendResponse(ResponseType::kFail, "Fail to read command", transport,
                          zx::error(ret.status_value()));
    }

    if (MatchCommand(command, "download")) {
      return Download(command, transport);
    }

    return ProcessCommand(command, transport);
  } else if (state_ == State::kDownload) {
    size_t packet_size = transport->PeekPacketSize();
    if (packet_size > remaining_download_size_) {
      ClearDownload();
      return SendResponse(ResponseType::kFail, "Unexpected amount of download", transport);
    }

    size_t offset = total_download_size() - remaining_download_size();
    uint8_t* start = reinterpret_cast<uint8_t*>(download_buffer_) + offset;
    if (zx::result<size_t> ret = transport->ReceivePacket(start, transport->PeekPacketSize());
        ret.is_error()) {
      ClearDownload();
      return SendResponse(ResponseType::kFail, "Failed to receive download packet", transport,
                          zx::error(ret.status_value()));
    }

    remaining_download_size_ -= packet_size;
    if (remaining_download_size_ == 0) {
      state_ = State::kCommand;
      return SendResponse(ResponseType::kOkay, "", transport);
    }

    return zx::ok();
  }

  return zx::ok();
}

void FastbootBase::ExtractCommandArgs(std::string_view cmd, const char* delimeter,
                                      CommandArgs& ret) {
  ret.num_args = 0;
  size_t start = 0;
  auto find_pos = cmd.find(delimeter, start);
  while (find_pos != std::string_view::npos && ret.num_args < kMaxCommandArgs) {
    // Skips empty argument.
    if (start < find_pos) {
      ret.args[ret.num_args++] = cmd.substr(start, find_pos - start);
    }
    start = find_pos + 1;
    find_pos = cmd.find(delimeter, start);
  }

  if (start < cmd.size()) {
    ret.args[ret.num_args++] = cmd.substr(start);
  }
}

zx::result<> FastbootBase::Download(std::string_view cmd, Transport* transport) {
  ClearDownload();
  CommandArgs args;
  ExtractCommandArgs(cmd, ":", args);
  if (args.num_args < 2) {
    return SendResponse(ResponseType::kFail, "Not enough argument", transport);
  }

  total_download_size_ = static_cast<size_t>(strtoul(args.args[1].data(), nullptr, 16));
  if (total_download_size_ == 0) {
    return SendResponse(ResponseType::kFail, "Empty size download is not allowed", transport);
  }

  zx::result<void*> buffer = GetDownloadBuffer(total_download_size_);
  if (buffer.is_error()) {
    ClearDownload();
    return SendResponse(ResponseType::kFail, "Failed to prepare download", transport,
                        zx::error(buffer.status_value()));
  }
  download_buffer_ = buffer.value();

  remaining_download_size_ = total_download_size_;
  state_ = State::kDownload;
  return SendDataResponse(total_download_size_, transport);
}

void FastbootBase::ClearDownload() {
  total_download_size_ = 0;
  remaining_download_size_ = 0;
  state_ = State::kCommand;
  download_buffer_ = nullptr;
  DoClearDownload();
}

}  // namespace fastboot
