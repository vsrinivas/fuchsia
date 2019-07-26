// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_PMT_H_
#define LIB_ZX_PMT_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

class pmt final : public object<pmt> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_PMT;

  constexpr pmt() = default;

  explicit pmt(zx_handle_t value) : object(value) {}

  explicit pmt(handle&& h) : object(h.release()) {}

  pmt(pmt&& other) : object(other.release()) {}

  pmt& operator=(pmt&& other) {
    reset(other.release());
    return *this;
  }

  zx_status_t unpin() { return zx_pmt_unpin(release()); }
};

using unowned_pmt = unowned<pmt>;

}  // namespace zx

#endif  // LIB_ZX_PMT_H_
