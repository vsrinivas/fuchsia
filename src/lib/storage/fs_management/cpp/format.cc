// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/include/fs-management/format.h"

#include <mutex>
#include <unordered_map>

#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace fs_management {
namespace {

class Registry {
 public:
  disk_format_t Register(std::unique_ptr<CustomDiskFormat> format) {
    std::scoped_lock lock(mutex_);
    int format_id = next_id_++;
    map_[format_id] = std::move(format);
    return static_cast<disk_format_t>(format_id);
  }

  const CustomDiskFormat* Get(disk_format_t format) {
    std::scoped_lock lock(mutex_);
    auto iter = map_.find(format);
    if (iter == map_.end()) {
      return nullptr;
    } else {
      return iter->second.get();
    }
  }

 private:
  std::mutex mutex_;
  int next_id_ FXL_GUARDED_BY(mutex_) = DISK_FORMAT_COUNT_;
  std::unordered_map<int, std::unique_ptr<CustomDiskFormat>> map_ FXL_GUARDED_BY(mutex_);
};

static Registry& GetRegistry() {
  static Registry& registry = *new Registry;
  return registry;
}

}  // namespace

disk_format_t CustomDiskFormat::Register(std::unique_ptr<CustomDiskFormat> format) {
  return GetRegistry().Register(std::move(format));
}

const CustomDiskFormat* CustomDiskFormat::Get(disk_format_t format) {
  return GetRegistry().Get(format);
}

}  // namespace fs_management

__EXPORT const char* disk_format_string(disk_format_t fs_type) {
  static const char* disk_format_string_[DISK_FORMAT_COUNT_] = {
      [DISK_FORMAT_UNKNOWN] = "unknown",
      [DISK_FORMAT_GPT] = "gpt",
      [DISK_FORMAT_MBR] = "mbr",
      [DISK_FORMAT_MINFS] = "minfs",
      [DISK_FORMAT_FAT] = "fat",
      [DISK_FORMAT_BLOBFS] = "blobfs",
      [DISK_FORMAT_FVM] = "fvm",
      [DISK_FORMAT_ZXCRYPT] = "zxcrypt",
      [DISK_FORMAT_FACTORYFS] = "factoryfs",
      [DISK_FORMAT_VBMETA] = "vbmeta",
      [DISK_FORMAT_BOOTPART] = "bootpart",
      [DISK_FORMAT_FXFS] = "fxfs",
      [DISK_FORMAT_F2FS] = "f2fs",
  };

  if (fs_type < DISK_FORMAT_COUNT_) {
    return disk_format_string_[fs_type];
  }

  auto format = fs_management::CustomDiskFormat::Get(fs_type);
  if (format == nullptr) {
    return "unknown!";
  }

  return format->name().c_str();
}
