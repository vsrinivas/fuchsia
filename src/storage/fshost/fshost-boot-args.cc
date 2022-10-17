// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/fshost-boot-args.h"

#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

namespace fshost {

// static
std::shared_ptr<FshostBootArgs> FshostBootArgs::Create() {
  return std::make_shared<FshostBootArgs>(
      fidl::WireSyncClient([]() -> fidl::ClientEnd<fuchsia_boot::Arguments> {
        zx::result local = component::Connect<fuchsia_boot::Arguments>();
        if (local.is_error()) {
          // This service might be missing if we're running in a test environment. Log
          // the error and continue.
          FX_LOGS(ERROR) << "failed to get boot arguments (" << local.status_string()
                         << "), assuming test "
                            "environment and continuing";
          return {};
        }
        return std::move(local.value());
      }()));
}

FshostBootArgs::FshostBootArgs(fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args)
    : boot_args_(std::move(boot_args)) {
  if (!boot_args_) {
    return;
  }

  fuchsia_boot::wire::BoolPair defaults[] = {
      {fidl::StringView{"netsvc.netboot"}, netsvc_netboot_},
      {fidl::StringView{"zircon.system.disable-automount"}, zircon_system_disable_automount_},
      {fidl::StringView{"zircon.system.filesystem-check"}, zircon_system_filesystem_check_},
  };
  auto ret =
      boot_args_->GetBools(fidl::VectorView<fuchsia_boot::wire::BoolPair>::FromExternal(defaults));
  if (!ret.ok()) {
    FX_LOGS(ERROR) << "failed to get boolean parameters: " << ret.error();
  } else {
    netsvc_netboot_ = ret.value().values[0];
    zircon_system_disable_automount_ = ret.value().values[1];
    zircon_system_filesystem_check_ = ret.value().values[2];
  }

  auto algorithm = GetStringArgument("blobfs.write-compression-algorithm");
  if (algorithm.is_error()) {
    if (algorithm.status_value() != ZX_ERR_NOT_FOUND) {
      FX_LOGS(ERROR) << "failed to get blobfs compression algorithm: " << algorithm.status_string();
    }
  } else {
    blobfs_write_compression_algorithm_ = std::move(algorithm).value();
  }

  auto eviction_policy = GetStringArgument("blobfs.cache-eviction-policy");
  if (eviction_policy.is_error()) {
    if (eviction_policy.status_value() != ZX_ERR_NOT_FOUND) {
      FX_LOGS(ERROR) << "failed to get blobfs eviction policy: " << eviction_policy.status_string();
    }
  } else {
    blobfs_eviction_policy_ = std::move(eviction_policy).value();
  }
}

zx::result<std::string> FshostBootArgs::GetStringArgument(const std::string& key) {
  if (!boot_args_) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  auto ret = boot_args_->GetString(fidl::StringView::FromExternal(key));
  if (!ret.ok()) {
    return zx::error(ret.status());
  }
  // fuchsia.boot.Arguments.GetString returns a "string?" value, so we need to check for null
  fidl::StringView value = ret.value().value;
  if (value.is_null()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(std::string(value.data(), value.size()));
}

zx::result<std::string> FshostBootArgs::block_verity_seal() {
  return GetStringArgument("factory_verity_seal");
}

}  // namespace fshost
