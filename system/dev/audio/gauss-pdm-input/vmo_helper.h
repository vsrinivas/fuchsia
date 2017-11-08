// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <zircon/types.h>
#include <zx/vmo.h>

namespace audio {
namespace gauss {

class VmoHelperBase {
public:
    zx_status_t AllocateVmo(size_t buffer_size);
    zx_status_t GetVmoRange(zx_paddr_t* start_address);
    zx_status_t Duplicate(uint32_t rights, zx::vmo* handle);
    void DestroyVmo();

protected:
    size_t buffer_size_;
    zx::vmo ring_buffer_vmo_;
    bool valid_ = false;
};

template <bool DEBUG>
class VmoHelper : public VmoHelperBase {
public:
    zx_status_t AllocateVmo(size_t buffer_size);
    void printoffsetinvmo(uint32_t offset);
    void DestroyVmo();
};

template <>
class VmoHelper<true> : public VmoHelperBase {
public:
    zx_status_t AllocateVmo(size_t buffer_size);
    void printoffsetinvmo(uint32_t offset);
    void DestroyVmo();

private:
    uintptr_t ring_buffer_virt_;
};
}
}
