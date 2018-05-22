// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/weak_stub_controller.h"

namespace fidl {
namespace internal {

WeakStubController::WeakStubController(StubController* controller)
    : ref_count_(1u), controller_(controller) {}

WeakStubController::~WeakStubController() = default;

void WeakStubController::AddRef() { ++ref_count_; }

void WeakStubController::Release() {
  if (--ref_count_ == 0)
    delete this;
}

void WeakStubController::Invalidate() { controller_ = nullptr; }

}  // namespace internal
}  // namespace fidl
