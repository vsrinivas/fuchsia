// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_BLOCK_DEVICE_MANAGER_H_
#define SRC_STORAGE_FSHOST_BLOCK_DEVICE_MANAGER_H_

#include <istream>
#include <memory>

#include "block-device.h"

namespace devmgr {

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

  // Options consist of a set of strings, most of which enable a specific matcher.
  struct Options {
    static constexpr char kBlobfs[] = "blobfs";      // Enables blobfs partition.
    static constexpr char kBootpart[] = "bootpart";  // Enables bootpart partitions.
    static constexpr char kDefault[] = "default";    // Expands to default options.
    static constexpr char kDurable[] = "durable";    // Enables durable partition.
    static constexpr char kFactory[] = "factory";    // Enables factory partition.
    static constexpr char kFvm[] = "fvm";            // Enables FVM devices.
    static constexpr char kGpt[] = "gpt";            // Enables GPT devices.
    static constexpr char kMbr[] = "mbr";            // Enables MBR devices.
    static constexpr char kMinfs[] = "minfs";        // Enables minfs partition.
    static constexpr char kNetboot[] =
        "netboot";  // Disables everything except fvm, gpt and bootpart.
    static constexpr char kNoZxcrypt[] = "no-zxcrypt";  // Disables zxcrypt for minfs partitions.
    static constexpr char kMinfsRamdisk[] =
        "minfs-ramdisk";  // Minfs is in FVM in a ram-disk, thus doesn't require zxcrypt.

    bool is_set(std::string_view option) const { return options.find(option) != options.end(); }

    std::set<std::string, std::less<>> options;
  };

  // Reads options from the stream which consist of one option per line. "default" means include the
  // default options, and lines with a leading '-' negate the option.
  static Options ReadOptions(std::istream& stream);
  static Options DefaultOptions();

  explicit BlockDeviceManager(const Options& options);

  // Attempts to match the device against configured matchers and proceeds to add the device if
  // it does.
  zx_status_t AddDevice(BlockDeviceInterface& device);

 private:
  // A vector of configured matchers.  First-to-match wins.
  std::vector<std::unique_ptr<Matcher>> matchers_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_BLOCK_DEVICE_MANAGER_H_
