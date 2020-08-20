// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FTL_RAW_NAND_IMAGE_H_
#define SRC_STORAGE_VOLUME_IMAGE_FTL_RAW_NAND_IMAGE_H_

#include <cstdint>

namespace storage::volume_image {

// Supported flags for |RawNandImageHeader::flags|.
enum class RawNandImageFlag : uint32_t {
  // When set, dictates that the partition should be entirely erased before flashing the contents of
  // this image.
  kRequireWipeBeforeFlash = 0x1,
};

// Supported image data formats.
enum class ImageFormat : uint32_t {
  // The entire data consists of a sequence of blocks, where each block is of size |page_size| +
  // |oob_size|. and these blocks represent the view as is from how the data should be flashed to
  // the device.
  kRawImage = 0,

  // Android sparse format, where each block is of size |page_size| + |oob_size|.
  kAndroidSparseImage = 1,
};

// Header that precedes a block image, whose block data is being augmented with Out Of Band(OOB) or
// Spare Area bytes.
struct RawNandImageHeader {
  // Identifies this header.
  static constexpr uint64_t kMagic = 0x12A17178711A711D;

  // Current major version.
  static constexpr uint32_t kMajorVersion = 1;

  // Current minor version.
  static constexpr uint32_t kMinorVersion = 1;

  // 64 bits reserved as an indicator of this prelude.
  uint64_t magic = kMagic;

  // Major version number for the format.
  // Crossing this version may break backwards compatibility.
  uint32_t version_major = kMajorVersion;

  // Min version number for the format.
  // Crossing this version will not break backwards compatibility.
  uint32_t version_minor = kMinorVersion;

  // Set of blacks to tweak behaviour during the flashing process.
  // Values must match |RawNandSparseImageFlag| enum.
  uint32_t flags = 0;

  // Format of the content following the RawNandImageHeader.
  ImageFormat format = ImageFormat::kAndroidSparseImage;

  // Page size used for the data written in the chosen image format.
  // Must be equal to the target device page size.
  uint32_t page_size = 0;

  // Out of band bytes used in data written in the chosen image format.
  // Must be small or equal to the target device OOB byte size.
  uint8_t oob_size = 0;

  // Reserved
  uint8_t reserved[3] = {0xFF, 0xFF, 0xFF};
} __attribute__((packed));

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FTL_RAW_NAND_IMAGE_H_
