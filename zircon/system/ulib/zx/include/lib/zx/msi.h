// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_MSI_H_
#define LIB_ZX_MSI_H_

#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/object.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <zircon/availability.h>
#include <zircon/types.h>

namespace zx {

// This wrapper encompasses both MsiInterruptDispatcher MsiDispatcher due
// to them only having static members and MsiInterruptDispatcher otherwise using the
// same interface as a zx::interrupt.
class msi final : public object<msi> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_MSI;

  constexpr msi() = default;

  explicit msi(zx_handle_t value) : object(value) {}

  explicit msi(handle&& h) : object(h.release()) {}

  msi(msi&& other) : object(other.release()) {}

  msi& operator=(msi&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t allocate(const resource& resource, uint32_t count, msi* result)
      ZX_AVAILABLE_SINCE(7);
  static zx_status_t create(const msi& msi, uint32_t options, uint32_t msi_id, const vmo& vmo,
                            size_t vmo_offset, interrupt* result) ZX_AVAILABLE_SINCE(7);
} ZX_AVAILABLE_SINCE(7);

using unowned_msi = unowned<msi> ZX_AVAILABLE_SINCE(7);

}  // namespace zx

#endif  // LIB_ZX_MSI_H_
