// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/block-device-manager.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>

#include <fs-management/format.h>

namespace devmgr {
namespace {

// Splits the path into a directory and the last component.
std::pair<std::string_view, std::string_view> SplitPath(std::string_view path) {
  size_t separator = path.rfind('/');
  if (separator != std::string::npos) {
    return std::make_pair(path.substr(0, separator), path.substr(separator + 1));
  } else {
    return std::make_pair(std::string_view(), path);
  }
}

// Matches anything that appears to have the given content and keeps track of the first device it
// finds.
class ContentMatcher : public BlockDeviceManager::Matcher {
 public:
  ContentMatcher(disk_format_t format) : format_(format) {}

  disk_format_t Match(const BlockDeviceInterface& device) override {
    if (device.content_format() == format_) {
      return format_;
    } else {
      return DISK_FORMAT_UNKNOWN;
    }
  }

  zx_status_t Add(BlockDeviceInterface& device) override {
    zx_status_t status = device.Add();
    if (status != ZX_OK) {
      return status;
    }
    if (path_.empty()) {
      path_ = device.topological_path();
    }
    return ZX_OK;
  }

  const std::string& path() const { return path_; }

 private:
  const disk_format_t format_;
  std::string path_;
};

// Matches devices that handle groups of partitions.
class PartitionMapMatcher : public ContentMatcher {
 public:
  // |suffix| is a device that is expected to appear when the driver is bound. For example, FVM,
  // will add a "/fvm" device before adding children whilst GPT won't add anything.  If
  // |ramdisk_required| is set, this matcher will only match against a ram-disk.
  PartitionMapMatcher(disk_format_t format, std::string_view suffix, bool ramdisk_required)
      : ContentMatcher(format), suffix_(suffix), ramdisk_required_(ramdisk_required) {}

  bool ramdisk_required() const { return ramdisk_required_; }

  disk_format_t Match(const BlockDeviceInterface& device) override {
    constexpr std::string_view kRamdiskPrefix = "/dev/misc/ramctl/";
    if (ramdisk_required_ &&
        device.topological_path().compare(0, kRamdiskPrefix.length(), kRamdiskPrefix) != 0) {
      return DISK_FORMAT_UNKNOWN;
    }
    return ContentMatcher::Match(device);
  }

  // Returns true if |device| is a child of the device matched by this matcher.
  bool IsChild(const BlockDeviceInterface& device) const {
    if (path().empty()) {
      return false;
    }
    // Child partitions should have topological paths of the form:
    //   .../<suffix>/<partition-name>/block
    auto [dir1, base1] = SplitPath(device.topological_path());
    if (base1 != "block") {
      return false;
    }
    auto [dir2, base2] = SplitPath(dir1);
    // base should be something like <partition-name>-p-1, but we ignore that.
    return path() + suffix_ == dir2;
  }

 private:
  const std::string suffix_;
  const bool ramdisk_required_;
};

// Matches a partition with a given name and expected type GUID.
class SimpleMatcher : public BlockDeviceManager::Matcher {
 public:
  SimpleMatcher(PartitionMapMatcher& map, std::string partition_name,
                const fuchsia_hardware_block_partition_GUID& type_guid, disk_format_t format)
      : map_(map), partition_name_(partition_name), type_guid_(type_guid), format_(format) {}

  disk_format_t Match(const BlockDeviceInterface& device) override {
    if (map_.IsChild(device) && device.partition_name() == partition_name_ &&
        !memcmp(&device.GetTypeGuid(), &type_guid_, sizeof(type_guid_))) {
      return format_;
    } else {
      return DISK_FORMAT_UNKNOWN;
    }
  }

 private:
  const PartitionMapMatcher& map_;
  const std::string partition_name_;
  const fuchsia_hardware_block_partition_GUID type_guid_;
  const disk_format_t format_;
};

// Matches a data partition, which is a Minfs partition backed by zxcrypt.
class MinfsMatcher : public BlockDeviceManager::Matcher {
 public:
  using PartitionNames = std::set<std::string, std::less<>>;
  enum class Variant {
    // A regular minfs partition backed by zxcrypt.
    kNormal,
    // A minfs partition not backed by zxcrypt.
    kNoZxcrypt,
    // Only attach and unseal the zxcrypt partition; doesn't mount minfs.
    kZxcryptOnly
  };

  static constexpr std::string_view kZxcryptSuffix = "/zxcrypt/unsealed/block";

  MinfsMatcher(const PartitionMapMatcher& map, PartitionNames partition_names,
               const fuchsia_hardware_block_partition_GUID& type_guid, Variant variant)
      : map_(map),
        partition_names_(std::move(partition_names)),
        type_guid_(type_guid),
        variant_(variant) {}

  static Variant GetVariantFromOptions(const BlockDeviceManager::Options& options) {
    if (options.is_set(BlockDeviceManager::Options::kNoZxcrypt)) {
      return Variant::kNoZxcrypt;
    } else {
      return Variant::kNormal;
    }
  }

  disk_format_t Match(const BlockDeviceInterface& device) override {
    if (expected_inner_path_.empty()) {
      if (map_.IsChild(device) &&
          partition_names_.find(device.partition_name()) != partition_names_.end() &&
          !memcmp(&device.GetTypeGuid(), &type_guid_, sizeof(type_guid_))) {
        switch (variant_) {
          case Variant::kNormal:
            return map_.ramdisk_required() ? DISK_FORMAT_MINFS : DISK_FORMAT_ZXCRYPT;
          case Variant::kNoZxcrypt:
            return DISK_FORMAT_MINFS;
          case Variant::kZxcryptOnly:
            return DISK_FORMAT_ZXCRYPT;
        }
      }
    } else if (variant_ == Variant::kNormal && device.topological_path() == expected_inner_path_ &&
               !memcmp(&device.GetTypeGuid(), &type_guid_, sizeof(type_guid_))) {
      return DISK_FORMAT_MINFS;
    }
    return DISK_FORMAT_UNKNOWN;
  }

  zx_status_t Add(BlockDeviceInterface& device) override {
    // If the volume doesn't appear to be zxcrypt, assume that it's because it was never formatted
    // as such, or the keys have been shredded, so skip straight to reformatting.  Strictly
    // speaking, it's not necessary, because attempting to unseal should trigger the same behaviour,
    // but the log messages in that case are scary.
    if (device.GetFormat() == DISK_FORMAT_ZXCRYPT) {
      if (device.content_format() != DISK_FORMAT_ZXCRYPT) {
        printf("fshost: Formatting as zxcrypt partition\n");
        zx_status_t status = device.FormatZxcrypt();
        if (status != ZX_OK) {
          return status;
        }
        // Set the reformat_ flag so that when the Minfs device appears we can skip straight to
        // reformatting it (and skip any fsck).  Again, this isn't strictly required because
        // mounting should fail and we'll reformat, but we can skip that when we know we need to
        // reformat.
        reformat_ = true;
      }
    } else if (reformat_) {
      // We formatted zxcrypt, so skip straight to formatting minfs.
      zx_status_t status = device.FormatFilesystem();
      if (status != ZX_OK) {
        return status;
      }
      reformat_ = false;
    }
    zx_status_t status = device.Add();
    if (status != ZX_OK) {
      return status;
    }
    if (device.GetFormat() == DISK_FORMAT_ZXCRYPT) {
      expected_inner_path_ = device.topological_path();
      expected_inner_path_.append(kZxcryptSuffix);
    }
    return ZX_OK;
  }

 private:
  const PartitionMapMatcher& map_;
  const PartitionNames partition_names_;
  const fuchsia_hardware_block_partition_GUID type_guid_;
  const Variant variant_;
  std::string expected_inner_path_;
  // If we reformat the zxcrypt device, this flag is set so that we know we should reformat the
  // minfs device when it appears.
  bool reformat_ = false;
};

// Matches the factory partition.
class FactoryfsMatcher : public BlockDeviceManager::Matcher {
 public:
  static constexpr std::string_view kVerityMutableSuffix = "/verity/mutable/block";
  static constexpr std::string_view kVerityVerifiedSuffix = "/verity/verified/block";

  FactoryfsMatcher(const PartitionMapMatcher& map) : map_(map) {}

  disk_format_t Match(const BlockDeviceInterface& device) override {
    static constexpr fuchsia_hardware_block_partition_GUID factory_type_guid =
        GPT_FACTORY_TYPE_GUID;
    if (base_path_.empty()) {
      if (map_.IsChild(device) &&
          !memcmp(&device.GetTypeGuid(), &factory_type_guid, sizeof(factory_type_guid)) &&
          device.partition_name() == "factory") {
        return DISK_FORMAT_BLOCK_VERITY;
      }
    } else if (!memcmp(&device.GetTypeGuid(), &factory_type_guid, sizeof(factory_type_guid)) &&
               (device.topological_path() == std::string(base_path_).append(kVerityMutableSuffix) ||
                device.topological_path() ==
                    std::string(base_path_).append(kVerityVerifiedSuffix))) {
      return DISK_FORMAT_FACTORYFS;
    }
    return DISK_FORMAT_UNKNOWN;
  }

  zx_status_t Add(BlockDeviceInterface& device) override {
    zx_status_t status = device.Add();
    if (status != ZX_OK) {
      return status;
    }
    base_path_ = device.topological_path();
    return ZX_OK;
  }

 private:
  const PartitionMapMatcher& map_;
  std::string base_path_;
};

// Matches devices that report flags with BLOCK_FLAG_BOOTPART set.
class BootpartMatcher : public BlockDeviceManager::Matcher {
 public:
  disk_format_t Match(const BlockDeviceInterface& device) override {
    fuchsia_hardware_block_BlockInfo info;
    zx_status_t status = device.GetInfo(&info);
    if (status != ZX_OK) {
      return DISK_FORMAT_UNKNOWN;
    }
    return info.flags & BLOCK_FLAG_BOOTPART ? DISK_FORMAT_BOOTPART : DISK_FORMAT_UNKNOWN;
  }
};

MinfsMatcher::PartitionNames GetMinfsPartitionNames() { return {"minfs", GUID_DATA_NAME, "data"}; }

}  // namespace

BlockDeviceManager::Options BlockDeviceManager::ReadOptions(std::istream& stream) {
  std::set<std::string, std::less<>> options;
  for (std::string line; std::getline(stream, line);) {
    if (line == Options::kDefault) {
      auto default_options = DefaultOptions();
      options.insert(default_options.options.begin(), default_options.options.end());
    }
    if (line.size() > 0) {
      if (line[0] == '-') {
        options.erase(line.substr(1));
      } else if (line[0] == '#') {
        // Treat as comment; ignore.
      } else {
        options.emplace(std::move(line));
      }
    }
  }
  return {.options = std::move(options)};
}

BlockDeviceManager::Options BlockDeviceManager::DefaultOptions() {
  return {.options = {Options::kBlobfs, Options::kBootpart, Options::kDurable, Options::kFactory,
                      Options::kFvm, Options::kGpt, Options::kMinfs}};
}

BlockDeviceManager::BlockDeviceManager(const Options& options) {
  static constexpr fuchsia_hardware_block_partition_GUID minfs_type_guid = GUID_DATA_VALUE;

  if (options.is_set(Options::kBootpart)) {
    matchers_.push_back(std::make_unique<BootpartMatcher>());
  }

  auto gpt = std::make_unique<PartitionMapMatcher>(DISK_FORMAT_GPT, "", /*ramdisk_required=*/false);
  auto fvm = std::make_unique<PartitionMapMatcher>(DISK_FORMAT_FVM, "/fvm",
                                                   options.is_set(Options::kFvmRamdisk));

  bool gpt_required = options.is_set(Options::kGpt);
  bool fvm_required = options.is_set(Options::kFvm);

  if (!options.is_set(Options::kNetboot)) {
    // GPT partitions:
    if (options.is_set(Options::kDurable)) {
      static constexpr fuchsia_hardware_block_partition_GUID durable_type_guid =
          GPT_DURABLE_TYPE_GUID;
      matchers_.push_back(std::make_unique<MinfsMatcher>(
          *gpt, MinfsMatcher::PartitionNames{GPT_DURABLE_NAME}, durable_type_guid,
          MinfsMatcher::GetVariantFromOptions(options)));
      gpt_required = true;
    }
    if (options.is_set(Options::kFactory)) {
      matchers_.push_back(std::make_unique<FactoryfsMatcher>(*gpt));
      gpt_required = true;
    }

    // FVM partitions:
    if (options.is_set(Options::kBlobfs)) {
      static constexpr fuchsia_hardware_block_partition_GUID blobfs_type_guid = GUID_BLOB_VALUE;
      matchers_.push_back(
          std::make_unique<SimpleMatcher>(*fvm, "blobfs", blobfs_type_guid, DISK_FORMAT_BLOBFS));
      fvm_required = true;
    }
    if (options.is_set(Options::kMinfs)) {
      matchers_.push_back(
          std::make_unique<MinfsMatcher>(*fvm, GetMinfsPartitionNames(), minfs_type_guid,
                                         MinfsMatcher::GetVariantFromOptions(options)));
      fvm_required = true;
    }
  }

  // The partition map matchers go last because they match on content.
  if (fvm_required) {
    std::unique_ptr<PartitionMapMatcher> non_ramdisk_fvm;
    if (options.is_set(Options::kFvmRamdisk)) {
      // Add another matcher for the non-ramdisk version of FVM.
      non_ramdisk_fvm = std::make_unique<PartitionMapMatcher>(DISK_FORMAT_FVM, "/fvm",
                                                              /*ramdisk_required=*/false);
      if (options.is_set(Options::kAttachZxcryptToNonRamdisk)) {
        matchers_.push_back(
            std::make_unique<MinfsMatcher>(*non_ramdisk_fvm, GetMinfsPartitionNames(),
                                           minfs_type_guid, MinfsMatcher::Variant::kZxcryptOnly));
      }
    }
    matchers_.push_back(std::move(fvm));
    if (non_ramdisk_fvm) {
      matchers_.push_back(std::move(non_ramdisk_fvm));
    }
  }
  if (gpt_required) {
    matchers_.push_back(std::move(gpt));
  }
  if (options.is_set(Options::kMbr)) {
    matchers_.push_back(
        std::make_unique<PartitionMapMatcher>(DISK_FORMAT_MBR, "", /*ramdisk_required=*/false));
  }
}

zx_status_t BlockDeviceManager::AddDevice(BlockDeviceInterface& device) {
  if (device.topological_path().empty()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  for (auto& matcher : matchers_) {
    disk_format_t format = matcher->Match(device);
    if (format != DISK_FORMAT_UNKNOWN) {
      device.SetFormat(format);
      return matcher->Add(device);
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace devmgr
