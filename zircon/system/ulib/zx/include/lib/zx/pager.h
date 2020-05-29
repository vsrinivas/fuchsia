// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_PAGER_H_
#define LIB_ZX_PAGER_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/port.h>
#include <lib/zx/vmo.h>

namespace zx {

class pager final : public object<pager> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_PAGER;

  constexpr pager() = default;

  explicit pager(zx_handle_t value) : object(value) {}

  explicit pager(handle&& h) : object(h.release()) {}

  pager(pager&& other) : object(other.release()) {}

  pager& operator=(pager&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint32_t options, pager* result);

  zx_status_t create_vmo(uint32_t options, const port& port, uint64_t key, uint64_t size,
                         vmo* result) const {
    return zx_pager_create_vmo(get(), options, port.get(), key, size,
                               result->reset_and_get_address());
  }

  zx_status_t detach_vmo(const vmo& vmo) const { return zx_pager_detach_vmo(get(), vmo.get()); }

  zx_status_t supply_pages(const vmo& pager_vmo, uint64_t offset, uint64_t length,
                           const vmo& aux_vmo, uint64_t aux_offset) const {
    return zx_pager_supply_pages(get(), pager_vmo.get(), offset, length, aux_vmo.get(), aux_offset);
  }

  zx_status_t op_range(uint32_t op, const vmo& pager_vmo, uint64_t offset, uint64_t length,
                       uint64_t data) const {
    return zx_pager_op_range(get(), op, pager_vmo.get(), offset, length, data);
  }
};

using unowned_pager = unowned<pager>;

}  // namespace zx

#endif  // LIB_ZX_PAGER_H_
