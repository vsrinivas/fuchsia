// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/types.h>
#include <lib/zx/vmo.h>

namespace audio {
namespace gauss {

class VmoHelperBase {
public:
    zx_status_t AllocateVmo(zx_handle_t bti, size_t buffer_size);
    zx_status_t GetVmoRange(zx_paddr_t* start_address);
    zx_status_t Duplicate(uint32_t rights, zx::vmo* handle);
    void DestroyVmo();

protected:
    io_buffer_t buffer_;
    bool valid_ = false;
};

template <bool DEBUG>
class VmoHelper : public VmoHelperBase {
public:
    zx_status_t AllocateVmo(zx_handle_t bti, size_t buffer_size);
    void printoffsetinvmo(uint32_t offset);
    void DestroyVmo();
};

template <>
class VmoHelper<true> : public VmoHelperBase {
public:
    zx_status_t AllocateVmo(zx_handle_t bti, size_t buffer_size);
    void printoffsetinvmo(uint32_t offset);
    void DestroyVmo();

private:
    uintptr_t ring_buffer_virt_;
};
}
}
