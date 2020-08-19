// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_RAM_DISK_H_
#define SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_RAM_DISK_H_

#include <lib/zx/status.h>

#include <string>

#include <ramdevice-client/ramdisk.h>

namespace isolated_devmgr {

// A thin wrapper around the ram-disk C API. Strictly speaking, this isn't specific to
// isolated-devmgr.
class RamDisk {
 public:
  // Creates a ram-disk with |block_count| blocks of |block_size| bytes.
  static zx::status<RamDisk> Create(int block_size, int block_count);

  RamDisk(RamDisk&& other) : client_(other.client_) { other.client_ = nullptr; }
  RamDisk& operator=(RamDisk&& other) {
    if (other.client_)
      ramdisk_destroy(client_);
    client_ = other.client_;
    client_ = nullptr;
    return *this;
  }

  ~RamDisk() {
    if (client_)
      ramdisk_destroy(client_);
  }

  ramdisk_client_t* client() const { return client_; }

  // Returns the path to the device.
  std::string path() const { return ramdisk_get_path(client_); }

  zx::status<> SleepAfter(uint64_t block_count) {
    return zx::make_status(ramdisk_sleep_after(client_, block_count));
  }

  zx::status<> Wake() { return zx::make_status(ramdisk_wake(client_)); }

 private:
  RamDisk(ramdisk_client_t* client) : client_(client) {}

  ramdisk_client_t* client_;
};

}  // namespace isolated_devmgr

#endif  // SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_RAM_DISK_H_
