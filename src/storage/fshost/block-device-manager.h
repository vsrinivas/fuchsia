// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_BLOCK_DEVICE_MANAGER_H_
#define SRC_STORAGE_FSHOST_BLOCK_DEVICE_MANAGER_H_

#include <istream>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include "src/storage/fshost/block-device-interface.h"
#include "src/storage/fshost/config.h"

namespace fshost {

// BlockDeviceManager contains the logic that decides what to do with devices that appear, i.e. what
// drivers to attach and what filesystems should be mounted.
class BlockDeviceManager {
 public:
  // Derived Matcher classes are able to match against a device.
  class Matcher {
   public:
    Matcher() = default;
    Matcher(const Matcher&) = delete;
    Matcher& operator=(const Matcher&) = delete;
    virtual ~Matcher() = default;

    // Returns the disk format that this device should be, or DISK_FORMAT_UNKNOWN if this matcher
    // does not recognize it.
    virtual disk_format_t Match(const BlockDeviceInterface& device) = 0;

    // By default, attempts to add the given device whose format should be known at this point.
    virtual zx_status_t Add(BlockDeviceInterface& device) { return device.Add(); }
  };

  // Does not take ownership of |config|, which must refer to a valid object that outlives this
  // object.
  explicit BlockDeviceManager(const Config* config);

  // Attempts to match the device against configured matchers and proceeds to add the device if
  // it does.
  zx_status_t AddDevice(BlockDeviceInterface& device);

  const Config* config() const { return &config_; }

 private:
  const Config& config_;

  // A vector of configured matchers.  First-to-match wins.
  std::vector<std::unique_ptr<Matcher>> matchers_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_BLOCK_DEVICE_MANAGER_H_
