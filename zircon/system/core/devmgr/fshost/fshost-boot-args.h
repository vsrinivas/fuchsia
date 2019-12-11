// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_BOOT_ARGS_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_BOOT_ARGS_H_

#include <lib/boot-args/boot-args.h>
#include <zircon/status.h>

namespace devmgr {

class FshostBootArgs {
 public:
  FshostBootArgs() : boot_args_(std::make_unique<devmgr::BootArgs>()) {
    zx_status_t status = devmgr::BootArgs::CreateFromArgumentsService(boot_args_.get());
    if (status != ZX_OK) {
      // This service might be missing if we're running in a test environment. Log
      // the error and continue.
      fprintf(stderr,
              "fshost: failed to get boot arguments (%s), assuming test "
              "environment and continuing\n",
              zx_status_get_string(status));
    }
  }

  bool netboot() {
    return boot_args_->GetBool("netsvc.netboot", false) ||
           boot_args_->GetBool("zircon.system.disable-automount", false);
  }

  bool check_filesystems() { return boot_args_->GetBool("zircon.system.filesystem-check", false); }

  bool wait_for_data() { return boot_args_->GetBool("zircon.system.wait-for-data", true); }

  const char* pkgfs_file_with_prefix_and_name(const char* prefix, const char* name) {
    char key[256];
    if (snprintf(key, sizeof(key), "zircon.system.pkgfs.file.%s%s", prefix, name) >=
        (int)sizeof(key)) {
      printf("fshost: failed to format pkgfs file boot argument key\n");
      return nullptr;
    }
    return boot_args_->Get(key);
  }

  const char* pkgfs_cmd() { return boot_args_->Get("zircon.system.pkgfs.cmd"); }

 protected:
  // Protected constructor for FshostBootArgs that allows injecting a
  // different BootArgs member, for use in unit tests.
  explicit FshostBootArgs(std::unique_ptr<devmgr::BootArgs> boot_args)
      : boot_args_(std::move(boot_args)) {}

 private:
  std::unique_ptr<devmgr::BootArgs> boot_args_;
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_BOOT_ARGS_H_
