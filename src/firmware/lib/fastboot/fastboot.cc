// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.buildinfo/cpp/wire.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fastboot/fastboot.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sys/component/cpp/service_client.h>
#include <zircon/status.h>

#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include <sdk/lib/syslog/cpp/macros.h>

#include "payload-streamer.h"
#include "sparse_format.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace fastboot {
namespace {

constexpr char kFastbootLogTag[] = __FILE__;

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

bool IsAndroidSparseImage(const void* img, size_t size) {
  if (size < sizeof(sparse_header_t)) {
    return false;
  }
  sparse_header_t header;
  memcpy(&header, img, sizeof(sparse_header_t));
  return header.magic == SPARSE_HEADER_MAGIC;
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
          .name = "flash",
          .cmd = &Fastboot::Flash,
      },
      {
          .name = "set_active",
          .cmd = &Fastboot::SetActive,
      },
      {
          .name = "reboot",
          .cmd = &Fastboot::Reboot,
      },
      {
          .name = "continue",
          .cmd = &Fastboot::Continue,
      },
      {
          .name = "reboot-bootloader",
          .cmd = &Fastboot::RebootBootloader,
      },
      {
          .name = "oem add-staged-bootloader-file",
          .cmd = &Fastboot::OemAddStagedBootloaderFile,
      },
  });
  return *kCommandTable;
}

const Fastboot::VariableHashTable& Fastboot::GetVariableTable() {
  // Using a static pointer and allocate with `new` so that the static instance
  // never gets deleted.
  static const VariableHashTable* kVariableTable = new VariableHashTable({
      {"max-download-size", &Fastboot::GetVarMaxDownloadSize},
      {"slot-count", &Fastboot::GetVarSlotCount},
      {"is-userspace", &Fastboot::GetVarIsUserspace},
      {"hw-revision", &Fastboot::GetVarHwRevision},
  });
  return *kVariableTable;
}

Fastboot::Fastboot(size_t max_download_size) : max_download_size_(max_download_size) {}

Fastboot::Fastboot(size_t max_download_size, fidl::ClientEnd<fuchsia_io::Directory> svc_root)
    : max_download_size_(max_download_size), svc_root_(std::move(svc_root)) {}

zx::status<> Fastboot::ProcessCommand(std::string_view command, Transport* transport) {
  for (const CommandEntry& cmd : GetCommandTable()) {
    if (MatchCommand(command, cmd.name)) {
      return (this->*cmd.cmd)(command.data(), transport);
    }
  }

  return SendResponse(ResponseType::kFail, "Unsupported command", transport);
}

void Fastboot::DoClearDownload() { download_vmo_mapper_.Reset(); }

zx::status<void*> Fastboot::GetDownloadBuffer(size_t total_download_size) {
  if (zx_status_t ret = download_vmo_mapper_.CreateAndMap(total_download_size, "fastboot download");
      ret != ZX_OK) {
    return zx::error(ret);
  }

  if (zx_status_t ret = download_vmo_mapper_.vmo().set_prop_content_size(total_download_size);
      ret != ZX_OK) {
    return zx::error(ret);
  }

  return zx::ok(download_vmo_mapper_.start());
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

zx::status<std::string> Fastboot::GetVarHwRevision(const std::vector<std::string_view>&,
                                                   Transport*) {
  auto svc_root = GetSvcRoot();
  if (svc_root.is_error()) {
    return zx::error(svc_root.status_value());
  }

  auto connect_result = component::ConnectAt<fuchsia_buildinfo::Provider>(*svc_root.value());
  if (connect_result.is_error()) {
    return zx::error(connect_result.status_value());
  }

  auto provider = fidl::WireSyncClient(std::move(connect_result.value()));

  auto resp = provider->GetBuildInfo();
  if (!resp.ok()) {
    return zx::error(resp.status());
  }

  return zx::ok(resp->build_info.board_config().data());
}

zx::status<std::string> Fastboot::GetVarSlotCount(const std::vector<std::string_view>&,
                                                  Transport* transport) {
  auto boot_manager_res = FindBootManager();
  if (boot_manager_res.is_error()) {
    auto ret = SendResponse(ResponseType::kFail, "Failed to find boot manager", transport,
                            zx::error(boot_manager_res.status_value()));
    return zx::error(ret.status_value());
  }
  // `fastboot set_active` only cares whether the device has >1 slots. Doesn't care how many
  // exactly.
  return boot_manager_res.value()->QueryCurrentConfiguration().ok() ? zx::ok("2") : zx::ok("1");
}

zx::status<std::string> Fastboot::GetVarIsUserspace(const std::vector<std::string_view>&,
                                                    Transport*) {
  return zx::ok("yes");
}

zx::status<fidl::ClientEnd<fuchsia_io::Directory>*> Fastboot::GetSvcRoot() {
  // If `svc_root_` is not set, use the system svc root.
  if (!svc_root_) {
    zx::channel request, service_root;
    zx_status_t status = zx::channel::create(0, &request, &service_root);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kFastbootLogTag)
          << "Failed to create channel " << zx_status_get_string(status);
      return zx::error(ZX_ERR_INTERNAL);
    }

    status = fdio_service_connect("/svc", request.release());
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kFastbootLogTag)
          << "Failed to connect to svc root " << zx_status_get_string(status);
      return zx::error(ZX_ERR_INTERNAL);
    }
    svc_root_ = fidl::ClientEnd<fuchsia_io::Directory>(std::move(service_root));
  }

  return zx::ok(&svc_root_);
}

zx::status<fidl::WireSyncClient<fuchsia_paver::Paver>> Fastboot::ConnectToPaver() {
  // Connect to the paver
  auto svc_root = GetSvcRoot();
  if (svc_root.is_error()) {
    return zx::error(svc_root.status_value());
  }

  auto paver_svc = component::ConnectAt<fuchsia_paver::Paver>(*svc_root.value());
  if (!paver_svc.is_ok()) {
    FX_LOGST(ERROR, kFastbootLogTag) << "Unable to open /svc/fuchsia.paver.Paver";
    return zx::error(paver_svc.error_value());
  }

  return zx::ok(fidl::WireSyncClient(std::move(*paver_svc)));
}

fuchsia_mem::wire::Buffer Fastboot::GetWireBufferFromDownload() {
  fuchsia_mem::wire::Buffer buf;
  buf.size = download_vmo_mapper_.size();
  buf.vmo = download_vmo_mapper_.Release();
  return buf;
}

zx::status<> Fastboot::WriteFirmware(fuchsia_paver::wire::Configuration config,
                                     std::string_view firmware_type, Transport* transport,
                                     fidl::WireSyncClient<fuchsia_paver::DataSink>& data_sink) {
  auto ret = data_sink->WriteFirmware(config, fidl::StringView::FromExternal(firmware_type),
                                      GetWireBufferFromDownload());
  if (ret.status() != ZX_OK) {
    return SendResponse(ResponseType::kFail, "Failed to invoke paver bootloader write", transport,
                        zx::error(ret.status()));
  }

  if (ret.value().result.is_status() && ret.value().result.status() != ZX_OK) {
    return SendResponse(ResponseType::kFail, "Failed to write bootloader", transport,
                        zx::error(ret.value().result.status()));
  }

  if (ret.value().result.is_unsupported() && ret.value().result.unsupported()) {
    return SendResponse(ResponseType::kFail, "Firmware type is not supported", transport);
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

zx::status<> Fastboot::WriteAsset(fuchsia_paver::wire::Configuration config,
                                  fuchsia_paver::wire::Asset asset, Transport* transport,
                                  fidl::WireSyncClient<fuchsia_paver::DataSink>& data_sink) {
  auto ret = data_sink->WriteAsset(config, asset, GetWireBufferFromDownload());
  zx_status_t status = ret.status() == ZX_OK ? ret.value().status : ret.status();
  if (status != ZX_OK) {
    return SendResponse(ResponseType::kFail, "Failed to flash asset", transport, zx::error(status));
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

zx::status<> Fastboot::Flash(const std::string& command, Transport* transport) {
  if (IsAndroidSparseImage(download_vmo_mapper_.start(), download_vmo_mapper_.size())) {
    return SendResponse(ResponseType::kFail, "Android sparse image is not supported.", transport);
  }

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
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)paver_client_res.value()->FindDataSink(std::move(data_sink_remote));
  fidl::WireSyncClient data_sink{std::move(data_sink_local)};

  FlashPartitionInfo info = GetPartitionInfo(args[1]);
  if (info.partition == "bootloader") {
    // If abr suffix is not given, assume that firmware ABR is not supported and just provide a
    // A slot configuration. It will be ignored by the paver.
    fuchsia_paver::wire::Configuration config =
        info.configuration ? *info.configuration : fuchsia_paver::wire::Configuration::kA;
    std::string_view firmware_type = args.size() == 3 ? args[2] : "";
    return WriteFirmware(config, firmware_type, transport, data_sink);
  } else if (info.partition == "fuchsia-esp") {
    // x64 platform uses 'fuchsia-esp' for bootloader partition . We should eventually move to use
    // "bootloader"
    // For legacy `fuchsia-esp` we don't consider firmware ABR or type.
    return WriteFirmware(fuchsia_paver::wire::Configuration::kA, "", transport, data_sink);
  } else if (info.partition == "zircon" && info.configuration) {
    return WriteAsset(*info.configuration, fuchsia_paver::wire::Asset::kKernel, transport,
                      data_sink);
  } else if (info.partition == "vbmeta" && info.configuration) {
    return WriteAsset(*info.configuration, fuchsia_paver::wire::Asset::kVerifiedBootMetadata,
                      transport, data_sink);
  } else if (info.partition == "fvm") {
    auto ret = data_sink->WriteOpaqueVolume(GetWireBufferFromDownload());

    zx_status_t status = ZX_OK;
    if (ret.status() != ZX_OK) {
      status = ret.status();
    } else if (ret->is_error()) {
      status = ret->error_value();
    }

    if (status != ZX_OK) {
      return SendResponse(ResponseType::kFail, "Failed to flash opaque fvm", transport,
                          zx::error(status));
    }

    return SendResponse(ResponseType::kOkay, "", transport);
  } else if (info.partition == "fvm.sparse") {
    // Flashing the sparse format FVM image via the paver. Note that at the time this code is
    // written, the format of FVM for fuchsia has not reached at a stable point yet. However, the
    // implementation of the paver fidl interface `WriteVolumes()` depends on the format of the FVM.
    // Therefore, it is important make sure that the device is running the latest version of paver
    // before using this fastboot command. This typically means flashing the latest kernel and
    // reboot first. Otherwise, if FVM format changes and the currently running paver is not
    // up-to-date, the FVM may be flashed wrongly.
    auto streamer_endpoints = fidl::CreateEndpoints<fuchsia_paver::PayloadStream>();
    if (streamer_endpoints.is_error()) {
      return SendResponse(ResponseType::kFail, "Failed to create payload streamer", transport,
                          zx::error(streamer_endpoints.status_value()));
    }
    auto [client, server] = std::move(*streamer_endpoints);

    // Launch thread which implements interface.
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    internal::PayloadStreamer streamer(std::move(server), download_vmo_mapper_.start(),
                                       download_vmo_mapper_.size());
    loop.StartThread("fastboot-payload-stream");

    auto result = data_sink->WriteVolumes(std::move(client));
    zx_status_t status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
      return SendResponse(ResponseType::kFail, "Failed to write fvm", transport, zx::error(status));
    }

    download_vmo_mapper_.Reset();
    return SendResponse(ResponseType::kOkay, "", transport);
  } else {
    return SendResponse(ResponseType::kFail, "Unsupported partition", transport);
  }

  return zx::ok();
}

zx::status<fidl::WireSyncClient<fuchsia_paver::BootManager>> Fastboot::FindBootManager() {
  auto paver_client_res = ConnectToPaver();
  if (!paver_client_res.is_ok()) {
    return zx::error(paver_client_res.status_value());
  }

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_paver::BootManager>();
  if (endpoints.is_error()) {
    FX_LOGST(ERROR, kFastbootLogTag) << "Failed to create endpoint";
    return zx::error(endpoints.status_value());
  }

  fidl::WireResult res = paver_client_res.value()->FindBootManager(std::move(endpoints->server));
  if (!res.ok()) {
    FX_LOGST(ERROR, kFastbootLogTag) << "Failed to find boot manager";
    return zx::error(res.status());
  }

  return zx::ok(fidl::WireSyncClient(std::move(endpoints->client)));
}

zx::status<> Fastboot::SetActive(const std::string& command, Transport* transport) {
  std::vector<std::string_view> args =
      fxl::SplitString(command, ":", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (args.size() < 2) {
    return SendResponse(ResponseType::kFail, "Not enough arguments", transport);
  }

  auto boot_manager_res = FindBootManager();
  if (boot_manager_res.is_error()) {
    return SendResponse(ResponseType::kFail, "Failed to find boot manager", transport,
                        zx::error(boot_manager_res.status_value()));
  }

  fuchsia_paver::wire::Configuration config = fuchsia_paver::wire::Configuration::kB;
  if (args[1] == "a") {
    config = fuchsia_paver::wire::Configuration::kA;
  } else if (args[1] != "b") {
    return SendResponse(ResponseType::kFail, "Invalid slot", transport);
  }

  auto result = boot_manager_res.value()->SetConfigurationActive(config);
  zx_status_t status = result.ok() ? result.value().status : result.status();
  if (status != ZX_OK) {
    return SendResponse(ResponseType::kFail, "Failed to set configuration active: ", transport,
                        zx::error(status));
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

zx::status<fidl::WireSyncClient<fuchsia_hardware_power_statecontrol::Admin>>
Fastboot::ConnectToPowerStateControl() {
  auto svc_root = GetSvcRoot();
  if (svc_root.is_error()) {
    return zx::error(svc_root.status_value());
  }

  auto connect_result =
      component::ConnectAt<fuchsia_hardware_power_statecontrol::Admin>(*svc_root.value());
  if (connect_result.is_error()) {
    return zx::error(connect_result.status_value());
  }

  return zx::ok(fidl::WireSyncClient(std::move(connect_result.value())));
}

zx::status<> Fastboot::Reboot(const std::string& command, Transport* transport) {
  auto connect_result = ConnectToPowerStateControl();
  if (connect_result.is_error()) {
    return SendResponse(ResponseType::kFail,
                        "Failed to connect to power state control service: ", transport,
                        zx::error(connect_result.status_value()));
  }

  // Send an okay response regardless of the result. Because once system reboots, we have
  // no chance to send any response.
  zx::status<> ret = SendResponse(ResponseType::kOkay, "", transport);
  if (ret.is_error()) {
    return ret;
  }
  // Wait for 1s to make sure the response is sent over to the transport
  std::this_thread::sleep_for(std::chrono::seconds(1));

  auto resp = connect_result.value()->Reboot(
      fuchsia_hardware_power_statecontrol::RebootReason::kUserRequest);
  if (!resp.ok()) {
    return zx::error(resp.status());
  }

  return zx::ok();
}

zx::status<> Fastboot::Continue(const std::string& command, Transport* transport) {
  zx::status<> ret = SendResponse(
      ResponseType::kInfo, "userspace fastboot cannot continue, rebooting instead", transport);
  if (ret.is_error()) {
    return ret;
  }

  return Reboot(command, transport);
}

zx::status<> Fastboot::RebootBootloader(const std::string& command, Transport* transport) {
  zx::status<> ret = SendResponse(
      ResponseType::kInfo,
      "userspace fastboot cannot reboot to bootloader, rebooting to recovery instead", transport);
  if (ret.is_error()) {
    return ret;
  }

  auto connect_result = ConnectToPowerStateControl();
  if (connect_result.is_error()) {
    return SendResponse(ResponseType::kFail,
                        "Failed to connect to power state control service: ", transport,
                        zx::error(connect_result.status_value()));
  }

  // Send an okay response regardless of the result. Because once system reboots, we have
  // no chance to send any response.
  ret = SendResponse(ResponseType::kOkay, "", transport);
  if (ret.is_error()) {
    return ret;
  }
  // Wait for 1s to make sure the response is sent over to the transport
  std::this_thread::sleep_for(std::chrono::seconds(1));

  auto resp = connect_result.value()->RebootToRecovery();
  if (!resp.ok()) {
    return zx::error(resp.status());
  }

  return zx::ok();
}

zx::status<> Fastboot::OemAddStagedBootloaderFile(const std::string& command,
                                                  Transport* transport) {
  std::vector<std::string_view> args =
      fxl::SplitString(command, " ", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (args.size() != 3) {
    return SendResponse(ResponseType::kFail, "Invalid number of arguments", transport);
  }

  if (args[2] != sshd_host::kAuthorizedKeysBootloaderFileName) {
    return SendResponse(ResponseType::kFail, "Unsupported file: " + std::string(args[2]),
                        transport);
  }

  auto svc_root = GetSvcRoot();
  if (svc_root.is_error()) {
    return zx::error(svc_root.status_value());
  }

  auto connect_result = component::ConnectAt<fuchsia_fshost::Admin>(*svc_root.value());
  if (connect_result.is_error()) {
    return SendResponse(ResponseType::kFail, "Failed to connect to fshost", transport,
                        zx::error(connect_result.status_value()));
  }

  fidl::WireSyncClient fshost_admin{std::move(connect_result.value())};
  auto resp = fshost_admin->WriteDataFile(
      fidl::StringView::FromExternal(sshd_host::kAuthorizedKeyPathInData),
      download_vmo_mapper_.Release());

  zx_status_t status = ZX_OK;
  if (resp.status() != ZX_OK) {
    status = resp.status();
  } else if (resp->is_error()) {
    status = resp->error_value();
  }

  if (status != ZX_OK) {
    return SendResponse(ResponseType::kFail, "Failed to write ssh key", transport,
                        zx::error(status));
  }

  return SendResponse(ResponseType::kOkay, "", transport);
}

}  // namespace fastboot
