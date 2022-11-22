// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/format.h"

#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <unistd.h>

#include <mutex>
#include <unordered_map>

#include <fbl/algorithm.h>
#include <pretty/hexdump.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/fs_management/cpp/path.h"

namespace fs_management {
namespace {

namespace fblock = fuchsia_hardware_block;

class Registry {
 public:
  DiskFormat Register(std::unique_ptr<CustomDiskFormat> format) {
    std::scoped_lock lock(mutex_);
    int format_id = next_id_++;
    map_[format_id] = std::move(format);
    return static_cast<DiskFormat>(format_id);
  }

  const CustomDiskFormat* Get(DiskFormat format) {
    std::scoped_lock lock(mutex_);
    auto iter = map_.find(format);
    if (iter == map_.end()) {
      return nullptr;
    }
    return iter->second.get();
  }

 private:
  std::mutex mutex_;
  int next_id_ FXL_GUARDED_BY(mutex_) = kDiskFormatCount;
  std::unordered_map<int, std::unique_ptr<CustomDiskFormat>> map_ FXL_GUARDED_BY(mutex_);
};

Registry& GetRegistry() {
  static Registry& registry = *new Registry;
  return registry;
}

enum DiskFormatLogVerbosity {
  Silent,
  Verbose,
};

DiskFormat DetectDiskFormatImpl(fidl::UnownedClientEnd<fblock::Block> device,
                                DiskFormatLogVerbosity verbosity) {
  const fidl::WireResult result = fidl::WireCall(device)->GetInfo();
  if (!result.ok()) {
    fprintf(stderr, "DetectDiskFormat: Could not acquire block device info: %s\n",
            result.FormatDescription().c_str());
    return kDiskFormatUnknown;
  }
  const fit::result response = result.value();
  if (response.is_error()) {
    fprintf(stderr, "DetectDiskFormat: Could not acquire block device info: %s\n",
            zx_status_get_string(response.error_value()));
    return kDiskFormatUnknown;
  }

  const fuchsia_hardware_block::wire::BlockInfo& info = response.value()->info;
  if (info.block_size == 0) {
    fprintf(stderr, "DetectDiskFormat: Expected a block size of > 0\n");
    return kDiskFormatUnknown;
  }

  // We need to read at least two blocks, because the GPT magic is located inside the second block
  // of the disk.
  size_t header_size = (kHeaderSize > (2 * info.block_size)) ? kHeaderSize : (2 * info.block_size);
  // check if the partition is big enough to hold the header in the first place
  if (header_size > info.block_size * info.block_count) {
    return kDiskFormatUnknown;
  }

  // We expect to read kHeaderSize bytes, but we may need to read
  // extra to read a multiple of the underlying block size.
  const size_t buffer_size = fbl::round_up(header_size, static_cast<size_t>(info.block_size));

  ZX_DEBUG_ASSERT_MSG(buffer_size > 0, "Expected buffer_size to be greater than 0\n");

  uint8_t data[buffer_size];
  {
    auto result = block_client::SingleReadBytes(device, data, buffer_size, 0);
    if (result != ZX_OK) {
      fprintf(stderr, "DetectDiskFormat: Error reading block device.\n");
      return kDiskFormatUnknown;
    }
  }

  if (!memcmp(data, kFvmMagic, sizeof(kFvmMagic))) {
    return kDiskFormatFvm;
  }

  if (!memcmp(data, kZxcryptMagic, sizeof(kZxcryptMagic))) {
    return kDiskFormatZxcrypt;
  }

  if (!memcmp(data, kBlockVerityMagic, sizeof(kBlockVerityMagic))) {
    return kDiskFormatBlockVerity;
  }

  if (!memcmp(data + info.block_size, kGptMagic, sizeof(kGptMagic))) {
    return kDiskFormatGpt;
  }

  if (!memcmp(data, kMinfsMagic, sizeof(kMinfsMagic))) {
    return kDiskFormatMinfs;
  }

  if (!memcmp(data, kBlobfsMagic, sizeof(kBlobfsMagic))) {
    return kDiskFormatBlobfs;
  }

  if (!memcmp(data, kFactoryfsMagic, sizeof(kFactoryfsMagic))) {
    return kDiskFormatFactoryfs;
  }

  if (!memcmp(data, kVbmetaMagic, sizeof(kVbmetaMagic))) {
    return kDiskFormatVbmeta;
  }

  if ((data[510] == 0x55 && data[511] == 0xAA)) {
    if ((data[38] == 0x29 || data[66] == 0x29)) {
      // 0x55AA are always placed at offset 510 and 511 for FAT filesystems.
      // 0x29 is the Boot Signature, but it is placed at either offset 38 or
      // 66 (depending on FAT type).
      return kDiskFormatFat;
    }
    return kDiskFormatMbr;
  }

  if (!memcmp(&data[1024], kF2fsMagic, sizeof(kF2fsMagic))) {
    return kDiskFormatF2fs;
  }

  if (!memcmp(data, kFxfsMagic, sizeof(kFxfsMagic))) {
    return kDiskFormatFxfs;
  }

  if (verbosity == DiskFormatLogVerbosity::Verbose) {
    // Log a hexdump of the bytes we looked at and didn't find any magic in.
    fprintf(stderr, "DetectDiskFormat: did not recognize format.  Looked at:\n");
    // fvm, zxcrypt, minfs, and blobfs have their magic bytes at the start
    // of the block.
    hexdump_very_ex(data, 16, 0, hexdump_stdio_printf, stderr);
    // MBR is two bytes at offset 0x1fe, but print 16 just for consistency
    hexdump_very_ex(data + 0x1f0, 16, 0x1f0, hexdump_stdio_printf, stderr);
    // GPT magic is stored one block in, so it can coexist with MBR.
    hexdump_very_ex(data + info.block_size, 16, info.block_size, hexdump_stdio_printf, stderr);
  }

  return kDiskFormatUnknown;
}

}  // namespace

__EXPORT
DiskFormat DetectDiskFormat(fidl::UnownedClientEnd<fblock::Block> device) {
  return DetectDiskFormatImpl(device, DiskFormatLogVerbosity::Silent);
}

__EXPORT
DiskFormat DetectDiskFormatLogUnknown(fidl::UnownedClientEnd<fblock::Block> device) {
  return DetectDiskFormatImpl(device, DiskFormatLogVerbosity::Verbose);
}

DiskFormat CustomDiskFormat::Register(std::unique_ptr<CustomDiskFormat> format) {
  return GetRegistry().Register(std::move(format));
}

const CustomDiskFormat* CustomDiskFormat::Get(DiskFormat format) {
  return GetRegistry().Get(format);
}

__EXPORT std::string_view DiskFormatString(DiskFormat fs_type) {
  switch (fs_type) {
    case kDiskFormatCount:
    case kDiskFormatUnknown:
      break;
    case kDiskFormatGpt:
      return "gpt";
    case kDiskFormatMbr:
      return "mbr";
    case kDiskFormatMinfs:
      return "minfs";
    case kDiskFormatFat:
      return "fat";
    case kDiskFormatBlobfs:
      return "blobfs";
    case kDiskFormatFvm:
      return "fvm";
    case kDiskFormatZxcrypt:
      return "zxcrypt";
    case kDiskFormatFactoryfs:
      return "factoryfs";
    case kDiskFormatBlockVerity:
      return "block verity";
    case kDiskFormatVbmeta:
      return "vbmeta";
    case kDiskFormatBootpart:
      return "bootpart";
    case kDiskFormatFxfs:
      return "fxfs";
    case kDiskFormatF2fs:
      return "f2fs";
    case kDiskFormatNandBroker:
      return "nand broker";
  }

  auto format = CustomDiskFormat::Get(fs_type);
  if (format == nullptr) {
    return "unknown!";
  }

  return format->name().c_str();
}

__EXPORT DiskFormat DiskFormatFromString(std::string_view str) {
  static auto* formats = [] {
    auto* formats = new std::unordered_map<std::string_view, DiskFormat>();
    for (auto format : {kDiskFormatGpt, kDiskFormatMbr, kDiskFormatMinfs, kDiskFormatFat,
                        kDiskFormatBlobfs, kDiskFormatFvm, kDiskFormatZxcrypt, kDiskFormatFactoryfs,
                        kDiskFormatBlockVerity, kDiskFormatVbmeta, kDiskFormatBootpart,
                        kDiskFormatFxfs, kDiskFormatF2fs, kDiskFormatNandBroker}) {
      formats->emplace(DiskFormatString(format), format);
    }
    return formats;
  }();
  auto iter = formats->find(str);
  if (iter == formats->end()) {
    return kDiskFormatUnknown;
  }
  return iter->second;
}

__EXPORT std::string_view DiskFormatComponentUrl(DiskFormat fs_type) {
  switch (fs_type) {
    case kDiskFormatBlobfs:
      return kBlobfsComponentUrl;
    case kDiskFormatFxfs:
      return kFxfsComponentUrl;
    case kDiskFormatMinfs:
      return kMinfsComponentUrl;
    case kDiskFormatF2fs:
      return kF2fsComponentUrl;
    case kDiskFormatCount:
    case kDiskFormatUnknown:
    case kDiskFormatGpt:
    case kDiskFormatMbr:
    case kDiskFormatFat:
    case kDiskFormatFvm:
    case kDiskFormatZxcrypt:
    case kDiskFormatFactoryfs:
    case kDiskFormatBlockVerity:
    case kDiskFormatVbmeta:
    case kDiskFormatBootpart:
    case kDiskFormatNandBroker:
      break;
  }

  auto format = CustomDiskFormat::Get(fs_type);
  if (format == nullptr) {
    return "";
  }

  return format->url().c_str();
}

__EXPORT std::string DiskFormatBinaryPath(DiskFormat fs_type) {
  switch (fs_type) {
    case kDiskFormatBlobfs:
      return GetBinaryPath("blobfs");
    case kDiskFormatMinfs:
      return GetBinaryPath("minfs");
    case kDiskFormatF2fs:
      return GetBinaryPath("f2fs");
    case kDiskFormatFat:
      return GetBinaryPath("fatfs");
    case kDiskFormatFactoryfs:
      return GetBinaryPath("factoryfs");
    case kDiskFormatFxfs:
    case kDiskFormatCount:
    case kDiskFormatUnknown:
    case kDiskFormatGpt:
    case kDiskFormatMbr:
    case kDiskFormatFvm:
    case kDiskFormatZxcrypt:
    case kDiskFormatBlockVerity:
    case kDiskFormatVbmeta:
    case kDiskFormatBootpart:
    case kDiskFormatNandBroker:
      break;
  }

  auto format = CustomDiskFormat::Get(fs_type);
  if (format == nullptr) {
    return "";
  }
  return format->binary_path();
}

}  // namespace fs_management
