// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_test.h"

#include "garnet/lib/ui/gfx/tests/mocks.h"

namespace scenic {
namespace gfx {
namespace test {

void SessionTest::SetUp() {
  engine_ = std::unique_ptr<Engine>(CreateEngine());
  session_ = fxl::MakeRefCounted<SessionForTest>(1, engine_.get(), this,
                                                 error_reporter());
}

void SessionTest::TearDown() {
  session_->TearDown();
  session_ = nullptr;
  engine_.reset();
}

std::unique_ptr<Engine> SessionTest::CreateEngine() {
  return std::make_unique<EngineForTest>(&display_manager_, nullptr);
}

void SessionTest::EnqueueEvent(fuchsia::ui::scenic::Event event) {
  events_.push_back(std::move(event));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
