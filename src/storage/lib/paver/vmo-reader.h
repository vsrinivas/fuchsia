// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_VMO_READER_H_
#define SRC_STORAGE_LIB_PAVER_VMO_READER_H_

#include <fidl/fuchsia.mem/cpp/wire.h>
#include <lib/zx/vmo.h>

#include <algorithm>

namespace paver {

class VmoReader {
 public:
  VmoReader(fuchsia_mem::wire::Buffer buffer) : vmo_(std::move(buffer.vmo)), size_(buffer.size) {}

  zx::result<size_t> Read(void* buf, size_t buf_size) {
    if (offset_ >= size_) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    const auto size = std::min(size_ - offset_, buf_size);
    auto status = zx::make_result(vmo_.read(buf, offset_, size));
    if (status.is_error()) {
      return status.take_error();
    }
    offset_ += size;
    return zx::ok(size);
  }

 private:
  zx::vmo vmo_;
  size_t size_;
  zx_off_t offset_ = 0;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_VMO_READER_H_
