// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_ADAPTER_MTD_WRITER_H_
#define SRC_STORAGE_VOLUME_IMAGE_ADAPTER_MTD_WRITER_H_

#include <lib/fpromise/result.h>

#include <string>
#include <string_view>
#include <utility>

#include "src/storage/volume_image/ftl/ftl_io.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

struct MtdParams {
  // Offset where to start writing in the device.
  uint64_t offset = 0;

  // Maximum number of bad blocks for usage in the underlying FTL.
  uint64_t max_bad_blocks = 0;

  // Whether the FTL contents should be formatted.
  bool format = false;
};

// Returns a writer into the underlying MTD(Memory Technology Device) protocol.
fpromise::result<std::unique_ptr<Writer>, std::string> CreateMtdWriter(std::string_view path,
                                                                       const MtdParams& params,
                                                                       FtlHandle* = nullptr);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_ADAPTER_MTD_WRITER_H_
