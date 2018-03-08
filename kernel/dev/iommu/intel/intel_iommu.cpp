// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/iommu/intel.h>
#include "iommu_impl.h"

zx_status_t IntelIommu::Create(fbl::unique_ptr<const uint8_t[]> desc, uint32_t desc_len,
                               fbl::RefPtr<Iommu>* out) {
    return intel_iommu::IommuImpl::Create(fbl::move(desc), desc_len, out);
}
