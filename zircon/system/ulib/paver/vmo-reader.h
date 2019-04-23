// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <algorithm>

#include <fbl/unique_ptr.h>
#include <fuchsia/mem/c/fidl.h>
#include <lib/zx/vmo.h>

namespace paver {

class VmoReader {
public:
    VmoReader(const fuchsia_mem_Buffer& buffer)
        : vmo_(buffer.vmo), size_(buffer.size) {}

    zx_status_t Read(void* buf, size_t buf_size, size_t* size_actual) {
        if (offset_ >= size_) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        const auto size = std::min(size_ - offset_, buf_size);
        auto status = vmo_.read(buf, offset_, size);
        if (status != ZX_OK) {
            return status;
        }
        offset_ += size;
        *size_actual = size;
        return ZX_OK;
    }

private:
    zx::vmo vmo_;
    size_t size_;
    zx_off_t offset_ = 0;
};

} // namespace paver
