// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/runtime_flag.h>

#include <atomic>
#include <cstdint>

namespace {

// TODO(39159): At this stage of wire-format migration, bindings default to sending the old
// union wire-format unless explicitly configured to send v1. Flip this to send v1 eventually.
// NOTE: the |{false}|-style direct initialization is to support C++14 compilers.
#ifdef FIDL_EXPERIMENTAL_WRITE_V1_WIREFORMAT
std::atomic<bool> write_union_as_xunion{true};
#else
std::atomic<bool> write_union_as_xunion{false};
#endif

}  // namespace

void fidl_global_set_should_write_union_as_xunion(bool enabled) {
  write_union_as_xunion.store(enabled);
}

bool fidl_global_get_should_write_union_as_xunion() { return write_union_as_xunion.load(); }
