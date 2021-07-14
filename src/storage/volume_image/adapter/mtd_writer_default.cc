// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/adapter/mtd_writer.h"

namespace storage::volume_image {

fpromise::result<std::unique_ptr<Writer>, std::string> CreateMtdWriter(std::string_view path,
                                                                       const MtdParams& params,
                                                                       FtlHandle* handle) {
  return fpromise::error("MtdWriter is only supported for linux platform.");
}

}  // namespace storage::volume_image
