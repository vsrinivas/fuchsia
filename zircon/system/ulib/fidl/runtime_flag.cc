// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/runtime_flag.h>

#include <atomic>
#include <cstdint>

// TODO(fxb/42311): remove this once everything is writing xunions
namespace {

std::atomic<bool> write_union_as_xunion{true};

}  // namespace

void fidl_global_set_should_write_union_as_xunion(bool enabled) {
  write_union_as_xunion.store(enabled);
}

bool fidl_global_get_should_write_union_as_xunion() { return write_union_as_xunion.load(); }
