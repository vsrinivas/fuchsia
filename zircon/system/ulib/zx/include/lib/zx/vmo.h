// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_VMO_H_
#define LIB_ZX_VMO_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/resource.h>

namespace zx {

class bti;

class vmo final : public object<vmo> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_VMO;

  constexpr vmo() = default;

  explicit vmo(zx_handle_t value) : object(value) {}

  explicit vmo(handle&& h) : object(h.release()) {}

  vmo(vmo&& other) : object(other.release()) {}

  vmo& operator=(vmo&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint64_t size, uint32_t options, vmo* result);
  static zx_status_t create_contiguous(const bti& bti, size_t size, uint32_t alignment_log2,
                                       vmo* result);
  static zx_status_t create_physical(const resource& resource, zx_paddr_t paddr, size_t size,
                                     vmo* result);

  zx_status_t read(void* data, uint64_t offset, size_t len) const {
    return zx_vmo_read(get(), data, offset, len);
  }

  zx_status_t write(const void* data, uint64_t offset, size_t len) const {
    return zx_vmo_write(get(), data, offset, len);
  }

  zx_status_t get_size(uint64_t* size) const { return zx_vmo_get_size(get(), size); }

  zx_status_t set_size(uint64_t size) const { return zx_vmo_set_size(get(), size); }

  zx_status_t create_child(uint32_t options, uint64_t offset, uint64_t size, vmo* result) const {
    // Allow for the caller aliasing |result| to |this|.
    vmo h;
    zx_status_t status =
        zx_vmo_create_child(get(), options, offset, size, h.reset_and_get_address());
    result->reset(h.release());
    return status;
  }

  zx_status_t op_range(uint32_t op, uint64_t offset, uint64_t size, void* buffer,
                       size_t buffer_size) const {
    return zx_vmo_op_range(get(), op, offset, size, buffer, buffer_size);
  }

  zx_status_t set_cache_policy(uint32_t cache_policy) const {
    return zx_vmo_set_cache_policy(get(), cache_policy);
  }

  zx_status_t replace_as_executable(const resource& vmex, vmo* result) {
    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t status = zx_vmo_replace_as_executable(value_, vmex.get(), &h);
    // We store ZX_HANDLE_INVALID to value_ before calling reset on result
    // in case result == this.
    value_ = ZX_HANDLE_INVALID;
    result->reset(h);
    return status;
  }
};

using unowned_vmo = unowned<vmo>;

}  // namespace zx

#endif  // LIB_ZX_VMO_H_
