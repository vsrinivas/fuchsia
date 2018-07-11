// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_
#define GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "garnet/lib/machina/block_dispatcher.h"

#if __aarch64__
#include "garnet/lib/machina/arch/arm64/gic_distributor.h"
#endif

struct BlockSpec {
  std::string path;
  machina::BlockDispatcher::Guid guid;

  machina::BlockDispatcher::Mode mode = machina::BlockDispatcher::Mode::RW;
  machina::BlockDispatcher::DataPlane data_plane =
      machina::BlockDispatcher::DataPlane::FDIO;
  bool volatile_writes = false;
};

enum class Kernel {
  ZIRCON,
  LINUX,
};

enum class GuestDisplay {
  FRAMEBUFFER,
  SCENIC,
  NONE,
};

class GuestConfig {
 public:
  Kernel kernel() const { return kernel_; }
  const std::string& kernel_path() const { return kernel_path_; }
  const std::string& ramdisk_path() const { return ramdisk_path_; }
  const std::string& cmdline() const { return cmdline_; }
  const std::string& dtb_overlay_path() const { return dtb_overlay_path_; }
  const std::vector<BlockSpec>& block_devices() const { return block_specs_; }
  bool block_wait() const { return block_wait_; }
  uint8_t num_cpus() const { return num_cpus_; }
  size_t memory() const { return memory_; }
  zx_duration_t balloon_interval() const {
    return ZX_SEC(balloon_interval_seconds_);
  }
  uint32_t balloon_pages_threshold() const { return balloon_pages_threshold_; }
  bool balloon_demand_page() const { return balloon_demand_page_; }
  GuestDisplay display() const { return display_; }
  bool network() const { return network_; }
#if __aarch64__
  machina::GicVersion gic_version() const { return gic_version_; }
#endif

 private:
  friend class GuestConfigParser;
  Kernel kernel_ = Kernel::ZIRCON;
  std::string kernel_path_;
  std::string ramdisk_path_;
  std::string cmdline_;
  std::string dtb_overlay_path_;
  std::vector<BlockSpec> block_specs_;
  bool block_wait_ = false;
  uint8_t num_cpus_ = zx_system_get_num_cpus();
  size_t memory_ = 1 << 30;
  uint32_t balloon_interval_seconds_ = 0;
  uint32_t balloon_pages_threshold_ = 0;
  bool balloon_demand_page_ = false;
  GuestDisplay display_ = GuestDisplay::SCENIC;
  bool network_ = true;
#if __aarch64__
  machina::GicVersion gic_version_ = machina::GicVersion::V2;
#endif
};

class GuestConfigParser {
 public:
  using OptionHandler = std::function<zx_status_t(const std::string& name,
                                                  const std::string& value)>;
  GuestConfigParser(GuestConfig* config);
  ~GuestConfigParser();

  zx_status_t ParseArgcArgv(int argc, char** argv);
  zx_status_t ParseConfig(const std::string& data);

 private:
  GuestConfig* cfg_;

  using OptionMap = std::unordered_map<std::string, OptionHandler>;
  OptionMap opts_;
};

#endif  // GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_
