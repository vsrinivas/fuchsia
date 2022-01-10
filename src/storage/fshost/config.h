// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_CONFIG_H_
#define SRC_STORAGE_FSHOST_CONFIG_H_

#include <istream>
#include <map>
#include <string_view>

#include "src/storage/fshost/fshost-boot-args.h"

namespace fshost {

// Fshost configuration is via a map of strings to values.  Most options are booleans and are
// considered true if present in the map.  This configuration is usually read from a configuration
// file
// (/pkg/config/fshost).  Some of the options are overridden by boot-arguments (see GetConfig).
struct Config {
 public:
  using Options = std::map<std::string, std::string, std::less<>>;

  // String constants for the keys in the config file. See the definitions in the .cc file for
  // documentation.
  static const char kApplyLimitsToRamdisk[];
  static const char kAttachZxcryptToNonRamdisk[];
  static const char kBlobfs[];
  static const char kBlobfsMaxBytes[];
  static const char kBootpart[];
  static const char kCheckFilesystems[];
  static const char kDefault[];
  static const char kDurable[];
  static const char kFactory[];
  static const char kFormatMinfsOnCorruption[];
  static const char kFvm[];
  static const char kFvmRamdisk[];
  static const char kGpt[];
  static const char kGptAll[];
  static const char kMbr[];
  static const char kMinfs[];
  static const char kMinfsMaxBytes[];
  static const char kNetboot[];
  static const char kNoZxcrypt[];
  static const char kSandboxDecompression[];
  static const char kUseDefaultLoader[];
  static const char kUseSyslog[];
  static const char kWaitForData[];
  static const char kDataFilesystemBinaryPath[];
  static const char kDataFilesystemUsesCrypt[];
  static const char kAllowLegacyDataPartitionNames[];
  static const char kNand[];

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

  // Reads the string option, defaulting to "" if not found.
  std::string ReadStringOptionValue(std::string_view key) const;

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

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_CONFIG_H_
