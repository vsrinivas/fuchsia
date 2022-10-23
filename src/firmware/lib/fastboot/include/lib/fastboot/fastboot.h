// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_H_
#define SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_H_

#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/result.h>
#include <stddef.h>
#include <zircon/status.h>

#include <string>
#include <string_view>
#include <unordered_map>

#include "fastboot_base.h"
#include "src/developer/sshd-host/constants.h"

namespace fastboot {

class __EXPORT Fastboot : public FastbootBase {
 public:
  explicit Fastboot(size_t max_download_size);
  // For test svc_root injection
  Fastboot(size_t max_download_size, fidl::ClientEnd<fuchsia_io::Directory> svc_root);

 private:
  size_t max_download_size_;
  fzl::OwnedVmoMapper download_vmo_mapper_;
  // Channel to svc.
  fidl::ClientEnd<fuchsia_io::Directory> svc_root_;

  zx::result<> ProcessCommand(std::string_view cmd, Transport *transport) override;
  zx::result<void *> GetDownloadBuffer(size_t total_download_size) override;
  void DoClearDownload() override;

  zx::result<> GetVar(const std::string &command, Transport *transport);
  zx::result<std::string> GetVarMaxDownloadSize(const std::vector<std::string_view> &, Transport *);
  zx::result<std::string> GetVarSlotCount(const std::vector<std::string_view> &, Transport *);
  zx::result<std::string> GetVarIsUserspace(const std::vector<std::string_view> &, Transport *);
  zx::result<std::string> GetVarHwRevision(const std::vector<std::string_view> &, Transport *);
  zx::result<> Flash(const std::string &command, Transport *transport);
  zx::result<> SetActive(const std::string &command, Transport *transport);
  zx::result<> Reboot(const std::string &command, Transport *transport);
  zx::result<> RebootBootloader(const std::string &command, Transport *transport);
  zx::result<> Continue(const std::string &command, Transport *transport);
  zx::result<> OemAddStagedBootloaderFile(const std::string &command, Transport *transport);

  zx::result<fidl::WireSyncClient<fuchsia_paver::Paver>> ConnectToPaver();
  zx::result<fidl::WireSyncClient<fuchsia_hardware_power_statecontrol::Admin>>
  ConnectToPowerStateControl();
  zx::result<fidl::WireSyncClient<fuchsia_paver::BootManager>> FindBootManager();
  zx::result<> WriteFirmware(fuchsia_paver::wire::Configuration config,
                             std::string_view firmware_type, Transport *transport,
                             fidl::WireSyncClient<fuchsia_paver::DataSink> &data_sink);
  zx::result<> WriteAsset(fuchsia_paver::wire::Configuration config,
                          fuchsia_paver::wire::Asset asset, Transport *transport,
                          fidl::WireSyncClient<fuchsia_paver::DataSink> &data_sink);

  struct CommandEntry {
    const char *name;
    zx::result<> (Fastboot::*cmd)(const std::string &, Transport *);
  };

  using VariableHashTable =
      std::unordered_map<std::string, zx::result<std::string> (Fastboot::*)(
                                          const std::vector<std::string_view> &, Transport *)>;

  // A static table of command name to method mapping.
  static const std::vector<CommandEntry> &GetCommandTable();

  // A static table of fastboot variable name to method mapping.
  static const VariableHashTable &GetVariableTable();

  zx::result<fidl::ClientEnd<fuchsia_io::Directory> *> GetSvcRoot();

  fuchsia_mem::wire::Buffer GetWireBufferFromDownload();

  friend class FastbootDownloadTest;
};

}  // namespace fastboot

#endif  // SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_H_
