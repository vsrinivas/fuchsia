// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_TESTING_RAM_DISK_H_
#define SRC_STORAGE_TESTING_RAM_DISK_H_

#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/gpt.h>

#include <array>
#include <optional>
#include <string>

#include <ramdevice-client/ramdisk.h>

namespace storage {

zx::result<> WaitForRamctl(zx::duration time = zx::duration::infinite());

// A thin wrapper around the ram-disk C API. Strictly speaking, this isn't specific to
// isolated-devmgr.
class RamDisk {
 public:
  struct Options {
    // If set, the ram-disk will report this type guid using the partition protocol.
    std::optional<std::array<uint8_t, GPT_GUID_LEN>> type_guid;
  };

  // Creates a ram-disk with |block_count| blocks of |block_size| bytes.
  static zx::result<RamDisk> Create(int block_size, uint64_t block_count,
                                    const Options& options = Options{});

  // Creates a ram-disk with the given VMO.  If block_size is zero, a default block size is used.
  static zx::result<RamDisk> CreateWithVmo(zx::vmo vmo, uint64_t block_size = 0);

  RamDisk() = default;
  RamDisk(RamDisk&& other) : client_(other.client_) { other.client_ = nullptr; }
  RamDisk& operator=(RamDisk&& other) {
    if (client_)
      ramdisk_destroy(client_);
    client_ = other.client_;
    other.client_ = nullptr;
    return *this;
  }

  ~RamDisk() {
    if (client_)
      ramdisk_destroy(client_);
  }

  ramdisk_client_t* client() const { return client_; }

  // Returns the path to the device.
  std::string path() const { return ramdisk_get_path(client_); }

  // Returns a channel to the device.
  zx::result<zx::channel> channel() const;

  zx::result<> SleepAfter(uint64_t block_count) {
    return zx::make_result(ramdisk_sleep_after(client_, block_count));
  }

  zx::result<> Wake() { return zx::make_result(ramdisk_wake(client_)); }

 private:
  RamDisk(ramdisk_client_t* client) : client_(client) {}

  ramdisk_client_t* client_ = nullptr;
};

}  // namespace storage

#endif  // SRC_STORAGE_TESTING_RAM_DISK_H_
