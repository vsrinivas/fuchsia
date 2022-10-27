// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_RAMDEVICE_CLIENT_CPP_INCLUDE_RAMDEVICE_CLIENT_RAMNAND_H_
#define SRC_LIB_STORAGE_RAMDEVICE_CLIENT_CPP_INCLUDE_RAMDEVICE_CLIENT_RAMNAND_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.nand/cpp/wire.h>
#include <inttypes.h>
#include <lib/zx/channel.h>
#include <zircon/compiler.h>

#include <memory>
#include <optional>

#include <fbl/string.h>

namespace ramdevice_client {

class RamNand {
 public:
  static constexpr char kBasePath[] = "/dev/sys/platform/00:00:2e/nand-ctl";

  // Creates a ram_nand under ram_nand_ctl running under the main devmgr.
  static zx_status_t Create(fuchsia_hardware_nand::wire::RamNandInfo config,
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

  const fidl::ClientEnd<fuchsia_device::Controller>& controller() const { return controller_; }
  const char* path() const {
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

  explicit RamNand(fidl::ClientEnd<fuchsia_device::Controller> controller)
      : controller_(std::move(controller)), path_(std::nullopt), filename_(std::nullopt) {}

 private:
  RamNand(fidl::ClientEnd<fuchsia_device::Controller> controller, fbl::String path,
          fbl::String filename)
      : controller_(std::move(controller)), path_(path), filename_(filename) {}

  fidl::ClientEnd<fuchsia_device::Controller> controller_;
  bool unbind = true;

  // Only valid if not spawned in an isolated devmgr.
  std::optional<fbl::String> path_;

  // Only valid if not spawned in an isolated devmgr.
  std::optional<fbl::String> filename_;
};

}  // namespace ramdevice_client

#endif  // SRC_LIB_STORAGE_RAMDEVICE_CLIENT_CPP_INCLUDE_RAMDEVICE_CLIENT_RAMNAND_H_
