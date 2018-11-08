// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_
#define GARNET_BIN_GUEST_VMM_GUEST_CONFIG_H_

#include <string>
#include <unordered_map>
#include <vector>

#include <fuchsia/guest/device/cpp/fidl.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

struct Guid {
  enum class Type {
    NONE,

    // Each GPT partition has 2 GUIDs, one that is unique to that specific
    // partition, and one that specifies the purpose of the partition.
    //
    // For a partial list of existing partition type GUIDs, see
    // https://en.wikipedia.org/wiki/GUID_Partition_Table#Partition_type_GUIDs
    GPT_PARTITION,
    GPT_PARTITION_TYPE,
  };

  Type type = Type::NONE;
  uint8_t bytes[GUID_LEN];

  // If |false|, |bytes| contains a valid GUID.
  bool empty() const { return type == Type::NONE; }
};

struct BlockSpec {
  std::string path;
  Guid guid;
  fuchsia::guest::device::BlockFormat format =
      fuchsia::guest::device::BlockFormat::RAW;
  fuchsia::guest::device::BlockMode mode =
      fuchsia::guest::device::BlockMode::READ_WRITE;
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
  uintptr_t host_address() const { return host_address_; }
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
  uintptr_t host_address_ = SIZE_MAX;
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
