// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_IOMMU_H_
#define LIB_ZX_IOMMU_H_

#include <lib/zx/object.h>
#include <lib/zx/resource.h>
#include <zircon/availability.h>

namespace zx {

class iommu final : public object<iommu> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_IOMMU;

  constexpr iommu() = default;

  explicit iommu(zx_handle_t value) : object(value) {}

  explicit iommu(handle&& h) : object(h.release()) {}

  iommu(iommu&& other) : object(other.release()) {}

  iommu& operator=(iommu&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(const resource& resource, uint32_t type, const void* desc,
                            size_t desc_size, iommu* result) ZX_AVAILABLE_SINCE(7);
} ZX_AVAILABLE_SINCE(7);

using unowned_iommu = unowned<iommu> ZX_AVAILABLE_SINCE(7);

}  // namespace zx

#endif  // LIB_ZX_IOMMU_H_
