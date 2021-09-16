// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/config.h"

#include <lib/syslog/cpp/macros.h>

#include <cinttypes>

namespace fshost {

// Normally the partition limits (minfs-max-bytes and blofs-max-bytes) apply only to non-ramdisk
// devices. This is to prevent device hardware configuration from leaking into ramdisk uses.
// When present, this flag makes them apply to all partitions of the corresponding type (this use
// used for tests).
const char Config::kApplyLimitsToRamdisk[] = "apply-limits-to-ramdisk";

// When set, allows zxcrypt to attach and unseal minfs partitions not in a ram-disk (but don't
// mount).
const char Config::kAttachZxcryptToNonRamdisk[] = "zxcrypt-non-ramdisk";

// Enables blobfs partitions.
const char Config::kBlobfs[] = "blobfs";

// Maximum number of bytes a blobfs partition can grow to. This applies only to non-ramdisk
// partitions unless "apply-limits-to-ramdisk" is set.
const char Config::kBlobfsMaxBytes[] = "blobfs-max-bytes";

// Enables bootpart partitions.
const char Config::kBootpart[] = "bootpart";

// Enables checking filesystems before mounting (if supported).
const char Config::kCheckFilesystems[] = "check-filesystems";

// Expands to default options. This will overwrite any previously-set options that have default
// values, so the order this appears in the file matters. Normally this would be the first line.
const char Config::kDefault[] = "default";

// Enables the durable partition (small partition of settings that survives factory resets).
const char Config::kDurable[] = "durable";

// Enables the factory partition (small partition of settings set in the factory and never written
// to otherwise).
const char Config::kFactory[] = "factory";

// Automatically formats minfs if it is found to be corrupted.
const char Config::kFormatMinfsOnCorruption[] = "format-minfs-on-corruption";

// Enables a single FVM device.
const char Config::kFvm[] = "fvm";

// FVM is in a ram-disk, thus minfs doesn't require zxcrypt.
const char Config::kFvmRamdisk[] = "fvm-ramdisk";

// Enables a single GPT device.
const char Config::kGpt[] = "gpt";

// Enables all GPT devices.
const char Config::kGptAll[] = "gpt-all";

// Enables MBR devices.
const char Config::kMbr[] = "mbr";

// Enables minfs partition.
const char Config::kMinfs[] = "minfs";

// Maximum number of bytes a minfs partition can grow to. This applies only to non-ramdisk
// partitions unless "apply-limits-to-ramdisk" is set.
const char Config::kMinfsMaxBytes[] = "minfs-max-bytes";

// Disables everything except fvm, gpt and bootpart.
const char Config::kNetboot[] = "netboot";

// Disables zxcrypt for minfs partitions.
const char Config::kNoZxcrypt[] = "no-zxcrypt";

// Perform decompression in a sandboxed component.
const char Config::kSandboxDecompression[] = "sandbox-decompression";

// Use the default loader rather than a custom one.
const char Config::kUseDefaultLoader[] = "use-default-loader";

// Wait for data before launching pkgfs.
const char Config::kWaitForData[] = "wait-for-data";

// Use Fxfs instead of Minfs for the data partition.
const char Config::kUseFxfs[] = "use-fxfs";

// Allow legacy names for the data partition.
const char Config::kAllowLegacyDataPartitionNames[] = "allow-legacy-data-partition-names";

Config::Options Config::ReadOptions(std::istream& stream) {
  Options options;
  for (std::string line; std::getline(stream, line);) {
    if (line == kDefault) {
      auto default_options = DefaultOptions();
      options.insert(default_options.begin(), default_options.end());
    } else if (line.size() > 0) {
      if (line[0] == '#')
        continue;  // Treat as comment; ignore;

      std::string key, value;
      if (auto separator_index = line.find('='); separator_index != std::string::npos) {
        key = line.substr(0, separator_index);
        value = line.substr(separator_index + 1);
      } else {
        key = line;  // No value, just a bare key.
      }

      if (key[0] == '-') {
        options.erase(key.substr(1));
      } else {
        options[key] = std::move(value);
      }
    }
  }
  return options;
}

Config::Options Config::DefaultOptions() {
  return {{kBlobfs, {}},
          {kBootpart, {}},
          {kFvm, {}},
          {kGpt, {}},
          {kMinfs, {}},
          {kFormatMinfsOnCorruption, {}},
          {kAllowLegacyDataPartitionNames, {}}};
}

uint64_t Config::ReadUint64OptionValue(std::string_view key, uint64_t default_value) const {
  auto found = options_.find(key);
  if (found == options_.end())
    return default_value;

  uint64_t value = 0;
  if (sscanf(found->second.c_str(), "%" PRIu64, &value) != 1) {
    FX_LOGS(ERROR) << "Can't read integer option value for " << key << ", got " << found->second;
    return default_value;
  }

  return value;
}

std::ostream& operator<<(std::ostream& stream, const Config::Options& options) {
  bool first = true;
  for (const auto& option : options) {
    if (first) {
      first = false;
    } else {
      stream << ", ";
    }
    stream << option.first;
    if (!option.second.empty()) {
      stream << "=" << option.second;
    }
  }
  return stream;
}

}  // namespace fshost
