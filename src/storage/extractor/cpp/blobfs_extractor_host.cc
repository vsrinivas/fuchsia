// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/extractor/cpp/extractor.h"

namespace extractor {

zx::result<> BlobfsExtract(fbl::unique_fd input_fd, Extractor& extractor) {
  return zx::error(ZX_ERR_ACCESS_DENIED);
}

}  // namespace extractor
