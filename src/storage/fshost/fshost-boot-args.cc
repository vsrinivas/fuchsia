// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/fshost-boot-args.h"

#include <lib/fdio/directory.h>
#include <zircon/errors.h>

namespace devmgr {

// static
std::shared_ptr<FshostBootArgs> FshostBootArgs::Create() {
  zx::channel remote, local;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    // This service might be missing if we're running in a test environment. Log
    // the error and continue.
    fprintf(stderr,
            "fshost: failed to get boot arguments (%s), assuming test "
            "environment and continuing\n",
            zx_status_get_string(status));
    return std::make_shared<FshostBootArgs>(std::nullopt);
  }
  auto path = fbl::StringPrintf("/svc/%s", llcpp::fuchsia::boot::Arguments::Name);
  status = fdio_service_connect(path.data(), remote.release());
  if (status != ZX_OK) {
    // This service might be missing if we're running in a test environment. Log
    // the error and continue.
    fprintf(stderr,
            "fshost: failed to get boot arguments (%s), assuming test "
            "environment and continuing\n",
            zx_status_get_string(status));
    return std::make_shared<FshostBootArgs>(std::nullopt);
  }
  return std::make_shared<FshostBootArgs>(
      llcpp::fuchsia::boot::Arguments::SyncClient(std::move(local)));
}

FshostBootArgs::FshostBootArgs(std::optional<llcpp::fuchsia::boot::Arguments::SyncClient> boot_args)
    : boot_args_(std::move(boot_args)) {
  if (!boot_args_) {
    return;
  }

  llcpp::fuchsia::boot::BoolPair defaults[] = {
      {fidl::StringView{"netsvc.netboot"}, netsvc_netboot_},
      {fidl::StringView{"zircon.system.disable-automount"}, zircon_system_disable_automount_},
      {fidl::StringView{"zircon.system.filesystem-check"}, zircon_system_filesystem_check_},
      {fidl::StringView{"zircon.system.wait-for-data"}, zircon_system_wait_for_data_},
      {fidl::StringView{"blobfs.userpager"}, blobfs_userpager_},
  };
  auto ret = boot_args_->GetBools(fidl::unowned_vec(defaults));
  if (!ret.ok()) {
    fprintf(stderr, "fshost: failed to get boolean parameters: %s", ret.error());
  } else {
    netsvc_netboot_ = ret->values[0];
    zircon_system_disable_automount_ = ret->values[1];
    zircon_system_filesystem_check_ = ret->values[2];
    zircon_system_wait_for_data_ = ret->values[3];
    blobfs_userpager_ = ret->values[4];
  }

  auto algorithm = GetStringArgument("blobfs.write-compression-algorithm");
  if (algorithm.is_error()) {
    fprintf(stderr, "fshost: failed to get blobfs compression algorithm: %s\n",
            algorithm.status_string());
  } else {
    blobfs_write_compression_algorithm_ = std::move(algorithm).value();
  }
}

zx::status<std::string> FshostBootArgs::GetStringArgument(std::string key) {
  if (!boot_args_) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  auto ret = boot_args_->GetString(fidl::unowned_str(key));
  if (!ret.ok()) {
    return zx::error(ret.status());
  }
  // fuchsia.boot.Arguments.GetString returns a "string?" value, so we need to check for null
  auto value = std::move(ret.value().value);
  if (value.is_null()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(std::string(value.data(), value.size()));
}

zx::status<std::string> FshostBootArgs::pkgfs_file_with_path(std::string path) {
  return GetStringArgument(std::string("zircon.system.pkgfs.file.") + path);
}

zx::status<std::string> FshostBootArgs::pkgfs_cmd() {
  return GetStringArgument("zircon.system.pkgfs.cmd");
}

zx::status<std::string> FshostBootArgs::block_verity_seal() {
  return GetStringArgument("factory_verity_seal");
}

}  // namespace devmgr
