// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_BOOT_ARGS_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_BOOT_ARGS_H_

#include <fcntl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <map>

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>

namespace devmgr {

class FshostBootArgs {
 public:
  FshostBootArgs() : boot_args_(nullptr) {
    zx::channel remote, local;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      // This service might be missing if we're running in a test environment. Log
      // the error and continue.
      fprintf(stderr,
              "fshost: failed to get boot arguments (%s), assuming test "
              "environment and continuing\n",
              zx_status_get_string(status));
      return;
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
      return;
    }
    boot_args_ = std::make_unique<llcpp::fuchsia::boot::Arguments::SyncClient>(std::move(local));
    InitParams();
  }

  bool netboot() { return netsvc_netboot_ || zircon_system_disable_automount_; }

  bool check_filesystems() { return zircon_system_filesystem_check_; }

  bool wait_for_data() { return zircon_system_wait_for_data_; }

  bool blobfs_enable_userpager() { return blobfs_userpager_; }

  bool blobfs_write_uncompressed() { return blobfs_uncompressed_; }

  std::unique_ptr<std::string> pkgfs_file_with_prefix_and_name(const char* prefix,
                                                               const char* name) {
    char key[256];
    int len = snprintf(key, sizeof(key), "zircon.system.pkgfs.file.%s%s", prefix, name);
    if (len >= (int)sizeof(key)) {
      printf("fshost: failed to format pkgfs file boot argument key\n");
      return nullptr;
    }
    if (!boot_args_) {
      return nullptr;
    }
    auto ret = boot_args_->GetString(fidl::StringView{key, (uint64_t)len});
    if (!ret.ok()) {
      return nullptr;
    }

    return std::make_unique<std::string>(ret->value.data(), ret->value.size());
  }

  std::unique_ptr<std::string> pkgfs_cmd() {
    if (!boot_args_)
      return nullptr;
    auto ret = boot_args_->GetString(fidl::StringView{"zircon.system.pkgfs.cmd"});
    if (!ret.ok() || ret->value.is_null()) {
      return nullptr;
    }

    return std::make_unique<std::string>(ret->value.data(), ret->value.size());
  }

 protected:
  // Protected constructor for FshostBootArgs that allows injecting a
  // different BootArgs member, for use in unit tests.
  explicit FshostBootArgs(std::unique_ptr<llcpp::fuchsia::boot::Arguments::SyncClient>&& boot_args)
      : boot_args_(std::move(boot_args)) {
    InitParams();
  }

 private:
  std::unique_ptr<llcpp::fuchsia::boot::Arguments::SyncClient> boot_args_;
  bool netsvc_netboot_ = false;
  bool zircon_system_disable_automount_ = false;
  bool zircon_system_filesystem_check_ = false;
  bool zircon_system_wait_for_data_ = true;
  bool blobfs_userpager_ = false;
  bool blobfs_uncompressed_ = false;

  void InitParams() {
    std::vector<llcpp::fuchsia::boot::BoolPair> defaults = {
        {fidl::StringView{"netsvc.netboot"}, false},
        {fidl::StringView{"zircon.system.disable-automount"}, false},
        {fidl::StringView{"zircon.system.filesystem-check"}, false},
        {fidl::StringView{"zircon.system.wait-for-data"}, true},
        {fidl::StringView{"blobfs.userpager"}, false},
        {fidl::StringView{"blobfs.uncompressed"}, false},
    };

    auto ret = boot_args_->GetBools(fidl::unowned_vec(defaults));
    if (!ret.ok()) {
      fprintf(stderr, "fshost: failed to get parameters: %s", ret.error());
      return;
    }

    netsvc_netboot_ = ret->values[0];
    zircon_system_disable_automount_ = ret->values[1];
    zircon_system_filesystem_check_ = ret->values[2];
    zircon_system_wait_for_data_ = ret->values[3];
    blobfs_userpager_ = ret->values[4];
    blobfs_uncompressed_ = ret->values[5];
  }
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_BOOT_ARGS_H_
