// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_EXCEPTION_H_
#define LIB_ZX_EXCEPTION_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace zx {

class exception final : public object<exception> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_EXCEPTION;

  constexpr exception() = default;

  explicit exception(zx_handle_t value) : object(value) {}

  explicit exception(handle&& h) : object(h.release()) {}

  exception(exception&& other) : object(other.release()) {}

  exception& operator=(exception&& other) {
    reset(other.release());
    return *this;
  }

  zx_status_t get_thread(thread* thread) const {
    return zx_exception_get_thread(get(), thread->reset_and_get_address());
  }

  zx_status_t get_process(process* process) const {
    return zx_exception_get_process(get(), process->reset_and_get_address());
  }
};

}  // namespace zx

#endif  // LIB_ZX_EXCEPTION_H_
