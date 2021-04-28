// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_GFX_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_GFX_TEST_H_

#include <memory>

#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/scenic/tests/scenic_test.h"
#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"

namespace scenic_impl::gfx::test {

class GfxSystemTest : public scenic_impl::test::ScenicTest {
 public:
  GfxSystemTest() = default;
  ~GfxSystemTest() override = default;

  // ::testing::Test virtual method.
  void TearDown() override;

  GfxSystem* gfx_system() { return gfx_system_.lock().get(); }
  Engine* engine() { return engine_.get(); }

 private:
  void InitializeScenic(std::shared_ptr<Scenic> scenic) override;

  std::shared_ptr<scheduling::DefaultFrameScheduler> frame_scheduler_;
  std::shared_ptr<Engine> engine_;

  std::weak_ptr<GfxSystem> gfx_system_;
};

}  // namespace scenic_impl::gfx::test

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_GFX_TEST_H_
