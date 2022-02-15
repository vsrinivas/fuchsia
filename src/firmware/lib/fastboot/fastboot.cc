// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/fastboot/fastboot.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include <optional>
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

struct FlashPartitionInfo {
  std::string_view partition;
  std::optional<fuchsia_paver::wire::Configuration> configuration;
};

FlashPartitionInfo GetPartitionInfo(std::string_view partition_label) {
  size_t len = partition_label.length();
  if (len < 2) {
    return {partition_label, std::nullopt};
  }

  FlashPartitionInfo ret;
  ret.partition = partition_label.substr(0, len - 2);
  std::string_view slot_suffix = partition_label.substr(len - 2, 2);
  if (slot_suffix == "_a") {
    ret.configuration = fuchsia_paver::wire::Configuration::kA;
  } else if (slot_suffix == "_b") {
    ret.configuration = fuchsia_paver::wire::Configuration::kB;
  } else if (slot_suffix == "_r") {
    ret.configuration = fuchsia_paver::wire::Configuration::kRecovery;
  } else {
    ret.partition = partition_label;
  }

  return ret;
}

}  // namespace

const std::vector<Fastboot::CommandEntry>& Fastboot::GetCommandTable() {
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
      {
          .name = "flash",
          .cmd = &Fastboot::Flash,
      },
      {
          .name = "set_active",
          .cmd = &Fastboot::SetActive,
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

Fastboot::Fastboot(size_t max_download_size, fidl::ClientEnd<fuchsia_io::Directory> svc_root)
    : max_download_size_(max_download_size), svc_root_(std::move(svc_root)) {}

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

zx::status<fidl::WireSyncClient<fuchsia_paver::Paver>> Fastboot::ConnectToPaver() {
  // If `svc_root_` is not set, use the system svc root.
  if (!svc_root_) {
    zx::channel request, service_root;
    zx_status_t status = zx::channel::create(0, &request, &service_root);
    if (status != ZX_OK) {
      FX_LOGF(ERROR, kFastbootLogTag, "Failed to create channel %s", zx_status_get_string(status));
      return zx::error(ZX_ERR_INTERNAL);
    }

    status = fdio_service_connect("/svc/.", request.release());
    if (status != ZX_OK) {
      FX_LOGF(ERROR, kFastbootLogTag, "Failed to connect to svc root %s",
              zx_status_get_string(status));
      return zx::error(ZX_ERR_INTERNAL);
    }
    svc_root_ = fidl::ClientEnd<fuchsia_io::Directory>(std::move(service_root));
  }

  // Connect to the paver
  auto paver_svc = service::ConnectAt<fuchsia_paver::Paver>(svc_root_);
  if (!paver_svc.is_ok()) {
    FX_LOGF(ERROR, kFastbootLogTag, "Unable to open /svc/fuchsia.paver.Paver");
    return zx::error(paver_svc.error_value());
  }

  return zx::ok(fidl::BindSyncClient(std::move(*paver_svc)));
}

zx::status<> Fastboot::WriteFirmware(fuchsia_paver::wire::Configuration config,
                                     std::string_view firmware_type, Transport* transport,
                                     fidl::WireSyncClient<fuchsia_paver::DataSink>& data_sink) {
  fuchsia_mem::wire::Buffer buf;
  buf.size = download_vmo_mapper_.size();
  buf.vmo = download_vmo_mapper_.Release();
  auto ret = data_sink->WriteFirmware(config, fidl::StringView::FromExternal(firmware_type),
                                      std::move(buf));
  if (ret.status() != ZX_OK) {
    return SendResponse(ResponseType::kFail, "Failed to invoke paver bootloader write", transport,
                        zx::error(ret.status()));
  }

  if (ret->result.is_status() && ret->result.status() != ZX_OK) {
    return SendResponse(ResponseType::kFail, "Failed to write bootloader", transport,
                        zx::error(ret->result.status()));
  }

  if (ret->result.is_unsupported() && ret->result.unsupported()) {
    return SendResponse(ResponseType::kFail, "Firmware type is not supported", transport);
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

zx::status<> Fastboot::WriteAsset(fuchsia_paver::wire::Configuration config,
                                  fuchsia_paver::wire::Asset asset, Transport* transport,
                                  fidl::WireSyncClient<fuchsia_paver::DataSink>& data_sink) {
  fuchsia_mem::wire::Buffer buf;
  buf.size = download_vmo_mapper_.size();
  buf.vmo = download_vmo_mapper_.Release();
  auto ret = data_sink->WriteAsset(config, asset, std::move(buf));
  zx_status_t status = ret.status() == ZX_OK ? ret.value().status : ret.status();
  if (status != ZX_OK) {
    return SendResponse(ResponseType::kFail, "Failed to flash asset", transport, zx::error(status));
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

zx::status<> Fastboot::Flash(const std::string& command, Transport* transport) {
  std::vector<std::string_view> args =
      fxl::SplitString(command, ":", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (args.size() < 2) {
    return SendResponse(ResponseType::kFail, "Not enough arguments", transport);
  }

  auto paver_client_res = ConnectToPaver();
  if (paver_client_res.is_error()) {
    return SendResponse(ResponseType::kFail, "Failed to connect to paver", transport,
                        zx::error(paver_client_res.status_value()));
  }

  // Connect to the data sink
  auto data_sink_endpoints = fidl::CreateEndpoints<fuchsia_paver::DataSink>();
  if (data_sink_endpoints.is_error()) {
    return SendResponse(ResponseType::kFail, "Unable to create data sink endpoint", transport,
                        zx::error(data_sink_endpoints.status_value()));
  }
  auto [data_sink_local, data_sink_remote] = std::move(*data_sink_endpoints);
  paver_client_res.value()->FindDataSink(std::move(data_sink_remote));
  auto data_sink = fidl::BindSyncClient(std::move(data_sink_local));

  FlashPartitionInfo info = GetPartitionInfo(args[1]);
  if (info.partition == "bootloader" && info.configuration) {
    std::string_view firmware_type = args.size() == 3 ? args[2] : "";
    return WriteFirmware(*info.configuration, firmware_type, transport, data_sink);
  } else if (info.partition == "zircon" && info.configuration) {
    return WriteAsset(*info.configuration, fuchsia_paver::wire::Asset::kKernel, transport,
                      data_sink);
  } else if (info.partition == "vbmeta" && info.configuration) {
    return WriteAsset(*info.configuration, fuchsia_paver::wire::Asset::kVerifiedBootMetadata,
                      transport, data_sink);
  } else {
    return SendResponse(ResponseType::kFail, "Unsupported partition", transport);
  }

  return zx::ok();
}

zx::status<> Fastboot::SetActive(const std::string& command, Transport* transport) {
  std::vector<std::string_view> args =
      fxl::SplitString(command, ":", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (args.size() < 2) {
    return SendResponse(ResponseType::kFail, "Not enough arguments", transport);
  }

  auto paver_client_res = ConnectToPaver();
  if (!paver_client_res.is_ok()) {
    return SendResponse(ResponseType::kFail, "Failed to connect to paver", transport,
                        zx::error(paver_client_res.status_value()));
  }

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_paver::BootManager>();
  if (endpoints.is_error()) {
    return SendResponse(ResponseType::kFail, "Failed to create zx channel", transport,
                        zx::error(endpoints.status_value()));
  }

  fidl::WireResult res = paver_client_res.value()->FindBootManager(std::move(endpoints->server));
  if (!res.ok()) {
    return SendResponse(ResponseType::kFail, "Failed to find boot manager", transport,
                        zx::error(endpoints.status_value()));
  }

  fidl::WireSyncClient<fuchsia_paver::BootManager> boot_manager =
      fidl::BindSyncClient(std::move(endpoints->client));

  fuchsia_paver::wire::Configuration config = fuchsia_paver::wire::Configuration::kB;
  if (args[1] == "a") {
    config = fuchsia_paver::wire::Configuration::kA;
  } else if (args[1] != "b") {
    return SendResponse(ResponseType::kFail, "Invalid slot", transport);
  }

  auto result = boot_manager->SetConfigurationActive(config);
  zx_status_t status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return SendResponse(
        ResponseType::kFail,
        "Failed to set configuration active: " + std::string(zx_status_get_string(status)),
        transport, zx::error(status));
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

}  // namespace fastboot
