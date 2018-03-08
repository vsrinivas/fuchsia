// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/iommu.h>
#include <fbl/ref_ptr.h>
#include <zircon/syscalls/iommu.h>

class IntelIommu {
public:
    static zx_status_t Create(fbl::unique_ptr<const uint8_t[]> desc, uint32_t desc_len,
                              fbl::RefPtr<Iommu>* out);
};
