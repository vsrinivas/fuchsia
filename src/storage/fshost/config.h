// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_CONFIG_H_
#define SRC_STORAGE_FSHOST_CONFIG_H_

#include <istream>
#include <map>
#include <string_view>

#include "src/storage/fshost/fshost-boot-args.h"

namespace devmgr {

// Fshost configuration is via a map of strings to values.  Most options are booleans and are
// considered true if present in the map.  This configuration is usually read from a configuration
// file
// (/pkg/config/fshost).  Some of the options are overridden by boot-arguments (see GetConfig).
struct Config {
 public:
  using Options = std::map<std::string, std::string, std::less<>>;

  static constexpr char kBlobfs[] = "blobfs";      // Enables blobfs partition.
  static constexpr char kBootpart[] = "bootpart";  // Enables bootpart partitions.
  static constexpr char kDefault[] = "default";    // Expands to default options.
  static constexpr char kDurable[] = "durable";    // Enables durable partition.
  static constexpr char kFactory[] = "factory";    // Enables factory partition.
  static constexpr char kFvm[] = "fvm";            // Enables a single FVM device.
  static constexpr char kGpt[] = "gpt";            // Enables a single GPT device.
  static constexpr char kGptAll[] = "gpt-all";     // Enables all GPT devices.
  static constexpr char kMbr[] = "mbr";            // Enables MBR devices.
  static constexpr char kMinfs[] = "minfs";        // Enables minfs partition.
  static constexpr char kBlobfsMaxBytes[] =
      "blobfs-max-bytes";  // Maximum number of bytes a blobfs partition can grow to.
  static constexpr char kMinfsMaxBytes[] =
      "minfs-max-bytes";  // Maximum number of bytes non-ramdisk minfs partition can grow to.
  static constexpr char kNetboot[] =
      "netboot";  // Disables everything except fvm, gpt and bootpart.
  static constexpr char kNoZxcrypt[] = "no-zxcrypt";  // Disables zxcrypt for minfs partitions.
  static constexpr char kFvmRamdisk[] =
      "fvm-ramdisk";  // FVM is in a ram-disk, thus minfs doesn't require zxcrypt.
  static constexpr char kAttachZxcryptToNonRamdisk[] =
      "zxcrypt-non-ramdisk";  // Attach and unseal zxcrypt to minfs partitions not in a ram-disk
  // (but don't mount).
  static constexpr char kFormatMinfsOnCorruption[] =
      "format-minfs-on-corruption";  // Formats minfs if it is found to be corrupted.
  static constexpr char kCheckFilesystems[] =
      "check-filesystems";  // Checks filesystems before mounting (if supported).
  static constexpr char kWaitForData[] = "wait-for-data";  // Wait for data before launching pkgfs.
  static constexpr char kUseSyslog[] = "use-syslog";       // Use syslog rather than debug-log.
  static constexpr char kUseDefaultLoader[] =
      "use-default-loader";  // Use the default loader rather than a custom one.
  static constexpr char kSandboxDecompression[] =
      "sandbox-decompression";  // Perform decompression in a sandboxed component.

  // Reads options from the stream which consist of one option per line. "default" means include the
  // default options, and lines with a leading '-' negate the option.
  static Options ReadOptions(std::istream& stream);

  // Returns the default options.
  static Options DefaultOptions();

  // Constructs with *no* options set (distinct from the default options).
  Config() = default;

  explicit Config(Options options) : options_(std::move(options)) {}

  // Movable but not copyable.
  Config(Config&&) = default;
  Config& operator=(Config&&) = default;

  bool is_set(std::string_view option) const { return options_.find(option) != options_.end(); }
  bool netboot() const { return is_set(kNetboot); }
  bool check_filesystems() const { return is_set(kCheckFilesystems); }
  bool wait_for_data() const { return is_set(kWaitForData); }

  // Reads the given named option, defaulting to the given value if not found.
  uint64_t ReadUint64OptionValue(std::string_view key, uint64_t default_value) const;

 private:
  friend std::ostream& operator<<(std::ostream& stream, const Config& config);

  // Key/value options. Many options do not have "values" so the value will be empty. This
  // will not contain the kDefault value; that's handled specially and causes the defaults to
  // be loaded.
  Options options_;
};

std::ostream& operator<<(std::ostream& stream, const Config::Options& options);
inline std::ostream& operator<<(std::ostream& stream, const Config& config) {
  return stream << config.options_;
}

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_CONFIG_H_
