// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <optional>

#include <inttypes.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <zircon/compiler.h>

namespace ramdevice_client {

class RamNand;

class RamNandCtl : public fbl::RefCounted<RamNandCtl> {
 public:
  // Creates an isolated devmgr and spawns a ram_nand_ctl device in it.
  static zx_status_t Create(fbl::RefPtr<RamNandCtl>* out);

  ~RamNandCtl() = default;

  const fbl::unique_fd& fd() { return ctl_; }
  const fbl::unique_fd& devfs_root() { return devmgr_.devfs_root(); }

 private:
  RamNandCtl(devmgr_integration_test::IsolatedDevmgr devmgr, fbl::unique_fd ctl)
      : devmgr_(std::move(devmgr)), ctl_(std::move(ctl)) {}

  devmgr_integration_test::IsolatedDevmgr devmgr_;
  fbl::unique_fd ctl_;
};

class RamNand {
 public:
  static constexpr char kBasePath[] = "/dev/misc/nand-ctl";

  // Creates a ram_nand under ram_nand_ctl running under the main devmgr.
  static zx_status_t Create(const fuchsia_hardware_nand_RamNandInfo* config,
                            std::optional<RamNand>* out);

  // Creates a ram_nand device underneath the ram_nand_ctl.
  static zx_status_t Create(fbl::RefPtr<RamNandCtl> ctl,
                            const fuchsia_hardware_nand_RamNandInfo* config,
                            std::optional<RamNand>* out);

  // Creates a ram_nand_ctl device and then a ram_device underneath.
  static zx_status_t CreateIsolated(const fuchsia_hardware_nand_RamNandInfo* config,
                                    std::optional<RamNand>* out);

  // Not copyable.
  RamNand(const RamNand&) = delete;
  RamNand& operator=(const RamNand&) = delete;

  // Movable.
  RamNand(RamNand&&) = default;
  RamNand& operator=(RamNand&&) = default;

  ~RamNand();

  // Don't unbind in destructor.
  void NoUnbind() { unbind = false; }

  const fbl::unique_fd& fd() { return fd_; }
  const char* path() {
    if (path_) {
      return path_->c_str();
    }
    return nullptr;
  }

  const char* filename() {
    if (filename_) {
      return filename_->c_str();
    }
    return nullptr;
  }

  const fbl::unique_fd& devfs_root() { return parent_->devfs_root(); }

 private:
  RamNand(fbl::unique_fd fd, fbl::RefPtr<RamNandCtl> ctl)
      : fd_(std::move(fd)), path_(std::nullopt), filename_(std::nullopt), parent_(ctl) {}

  RamNand(fbl::unique_fd fd, fbl::String path, fbl::String filename)
      : fd_(std::move(fd)), path_(path), filename_(filename), parent_(nullptr) {}

  fbl::unique_fd fd_;
  bool unbind = true;

  // Only valid if not spawned in an isolated devmgr.
  std::optional<fbl::String> path_;

  // Only valid if not spawned in an isolated devmgr.
  std::optional<fbl::String> filename_;

  // Optional parent if spawned in an isolated devmgr.
  fbl::RefPtr<RamNandCtl> parent_;
};

}  // namespace ramdevice_client
