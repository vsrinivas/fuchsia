// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_
#define GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_

#include <string>
#include <unordered_map>
#include <vector>

#include <fuchsia/guest/cpp/fidl.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

struct BlockSpec {
  std::string path;
  fuchsia::guest::BlockFormat format = fuchsia::guest::BlockFormat::RAW;
  fuchsia::guest::BlockMode mode = fuchsia::guest::BlockMode::READ_WRITE;
};

enum class Kernel {
  ZIRCON,
  LINUX,
};

class GuestConfig {
 public:
  Kernel kernel() const { return kernel_; }
  const std::string& kernel_path() const { return kernel_path_; }
  const std::string& ramdisk_path() const { return ramdisk_path_; }
  const std::string& cmdline() const { return cmdline_; }
  const std::string& dtb_overlay_path() const { return dtb_overlay_path_; }
  const std::vector<BlockSpec>& block_devices() const { return block_specs_; }
  uint8_t cpus() const { return cpus_; }
  size_t memory() const { return memory_; }
  size_t wl_memory() const { return wl_memory_; }
  bool host_memory() const { return host_memory_; }
  bool legacy_net() const { return legacy_net_; }
  bool virtio_balloon() const { return virtio_balloon_; }
  bool virtio_console() const { return virtio_console_; }
  bool virtio_gpu() const { return virtio_gpu_; }
  bool virtio_net() const { return virtio_net_; }
  bool virtio_rng() const { return virtio_rng_; }
  bool virtio_vsock() const { return virtio_vsock_; }
  bool virtio_wl() const { return virtio_wl_; }

 private:
  friend class GuestConfigParser;
  Kernel kernel_ = Kernel::ZIRCON;
  std::string kernel_path_;
  std::string ramdisk_path_;
  std::string cmdline_;
  std::string dtb_overlay_path_;
  std::vector<BlockSpec> block_specs_;
  uint8_t cpus_ = zx_system_get_num_cpus();
  size_t memory_ = 1 << 30;
  size_t wl_memory_ = 1 << 30;
  bool host_memory_ = false;
  bool legacy_net_ = true;
  bool virtio_balloon_ = true;
  bool virtio_console_ = true;
  bool virtio_gpu_ = true;
  bool virtio_net_ = true;
  bool virtio_rng_ = true;
  bool virtio_vsock_ = true;
  bool virtio_wl_ = true;
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
