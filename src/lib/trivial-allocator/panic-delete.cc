// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

namespace {

void DummyDelete() { ZX_PANIC("`delete` statement reached at runtime!"); }

}  // namespace

#define STUB_BODY DummyDelete();

#include "stub-delete.cc"  // nogncheck
