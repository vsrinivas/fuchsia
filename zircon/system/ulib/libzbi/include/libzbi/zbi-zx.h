// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "zbi-cpp.h"

#include <lib/zx/vmo.h>

namespace zbi {

class ZbiVMO : public Zbi {
public:
    ZbiVMO& operator=(const ZbiVMO&) = delete;

    zx_status_t Init(zx::vmo vmo);
    zx::vmo Release();

    ~ZbiVMO();

    // This resizes the VMO as needed.
    zbi_result_t AppendSection(uint32_t length, uint32_t type, uint32_t extra,
                               uint32_t flags, const void* payload);

    // This too.  The payload pointer is only valid until the next call to
    // AppendSection or CreateSection.
    zbi_result_t CreateSection(uint32_t length, uint32_t type, uint32_t extra,
                               uint32_t flags, void** payload);

    // Check and split a complete ZBI into kernel and data parts in new VMOs.
    zbi_result_t SplitComplete(ZbiVMO* kernel, ZbiVMO* data) const;

private:
    zx::vmo vmo_;

    friend zbi_result_t SplitCompleteWrapper(zx_handle_t zbi_vmo,
                                             zx_handle_t* kernel_vmo,
                                             zx_handle_t* data_vmo);

    zx_status_t Map();
    void Unmap();
};

} // namespace zbi
