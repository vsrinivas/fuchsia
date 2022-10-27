// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_FORMAT_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_FORMAT_H_

#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <string_view>

namespace fs_management {

constexpr std::string_view kBlobfsComponentUrl = "#meta/blobfs.cm";
constexpr std::string_view kFxfsComponentUrl = "#meta/fxfs.cm";
constexpr std::string_view kMinfsComponentUrl = "#meta/minfs.cm";
constexpr std::string_view kF2fsComponentUrl = "#meta/f2fs.cm";

enum DiskFormat {
  kDiskFormatUnknown = 0,
  kDiskFormatGpt = 1,
  kDiskFormatMbr = 2,
  kDiskFormatMinfs = 3,
  kDiskFormatFat = 4,
  kDiskFormatBlobfs = 5,
  kDiskFormatFvm = 6,
  kDiskFormatZxcrypt = 7,
  kDiskFormatFactoryfs = 8,
  kDiskFormatBlockVerity = 9,
  kDiskFormatVbmeta = 10,
  kDiskFormatBootpart = 11,
  kDiskFormatFxfs = 12,
  kDiskFormatF2fs = 13,
  kDiskFormatNandBroker = 14,
  kDiskFormatCount,
};

std::string_view DiskFormatString(DiskFormat fs_type);
DiskFormat DiskFormatFromString(std::string_view str);

// Get the component url for the disk format, if it's known. If it's not known (i.e. the format
// doesn't run as a component), this returns an empty string.
std::string_view DiskFormatComponentUrl(DiskFormat fs_type);

// Get the binary path for the disk format, if it's known.  If it's not known (i.e. the format can
// only be run as a component), this returns an empty string.
std::string DiskFormatBinaryPath(DiskFormat fs_type);

inline constexpr int kHeaderSize = 4096;

inline constexpr uint8_t kMinfsMagic[16] = {
    0x21, 0x4d, 0x69, 0x6e, 0x46, 0x53, 0x21, 0x00, 0x04, 0xd3, 0xd3, 0xd3, 0xd3, 0x00, 0x50, 0x38,
};

inline constexpr uint8_t kBlobfsMagic[16] = {
    0x21, 0x4d, 0x69, 0x9e, 0x47, 0x53, 0x21, 0xac, 0x14, 0xd3, 0xd3, 0xd4, 0xd4, 0x00, 0x50, 0x98,
};

inline constexpr uint8_t kGptMagic[16] = {
    0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54, 0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00,
};

inline constexpr uint8_t kFvmMagic[8] = {
    0x46, 0x56, 0x4d, 0x20, 0x50, 0x41, 0x52, 0x54,
};

inline constexpr uint8_t kZxcryptMagic[16] = {
    0x5f, 0xe8, 0xf8, 0x00, 0xb3, 0x6d, 0x11, 0xe7, 0x80, 0x7a, 0x78, 0x63, 0x72, 0x79, 0x70, 0x74,
};

inline constexpr uint8_t kBlockVerityMagic[16] = {0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x2d, 0x76, 0x65,
                                                  0x72, 0x69, 0x74, 0x79, 0x2d, 0x76, 0x31, 0x00};
inline constexpr uint8_t kFactoryfsMagic[8] = {
    0x21, 0x4d, 0x69, 0x1e, 0xF9, 0x3F, 0x5D, 0xA5,
};

inline constexpr uint8_t kVbmetaMagic[4] = {
    'A',
    'V',
    'B',
    '0',
};

inline constexpr uint8_t kF2fsMagic[4] = {
    0x10,
    0x20,
    0xf5,
    0xf2,
};

inline constexpr uint8_t kFxfsMagic[8] = {'F', 'x', 'f', 's', 'S', 'u', 'p', 'r'};

DiskFormat DetectDiskFormat(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device);
DiskFormat DetectDiskFormatLogUnknown(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device);

class __EXPORT CustomDiskFormat {
 public:
  static DiskFormat Register(std::unique_ptr<CustomDiskFormat> format);
  static const CustomDiskFormat* Get(DiskFormat);

  CustomDiskFormat(std::string name, std::string_view binary_path,
                   std::string_view component_url = "")
      : name_(std::move(name)), binary_path_(binary_path), component_url_(component_url) {}
  CustomDiskFormat(CustomDiskFormat&&) = default;

  const std::string& name() const { return name_; }
  const std::string& binary_path() const { return binary_path_; }
  const std::string& url() const { return component_url_; }

 private:
  std::string name_;
  std::string binary_path_;
  std::string component_url_;
};

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_FORMAT_H_
