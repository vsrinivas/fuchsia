// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include "gtest/gtest.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/bin/root_presenter/app.h"

namespace root_presenter {
namespace {

class RootPresenterTest : public gtest::TestLoopFixture {
 public:
  void SetUp() final { root_presenter_ = std::make_unique<App>(command_line_, dispatcher()); }
  void TearDown() final { root_presenter_.reset(); }

  App* root_presenter() { return root_presenter_.get(); }

 private:
  const fxl::CommandLine command_line_;
  std::unique_ptr<App> root_presenter_;
};

TEST_F(RootPresenterTest, SinglePresentView_ShouldSucceed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation;
  bool alive = true;
  presentation.set_error_handler([&alive](auto) { alive = false; });
  root_presenter()->PresentView(std::move(view_holder_token), presentation.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(alive);
}

TEST_F(RootPresenterTest, SecondPresentView_ShouldFail_AndOriginalShouldSurvive) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation1;
  bool alive1 = true;
  presentation1.set_error_handler([&alive1](auto) { alive1 = false; });
  root_presenter()->PresentView(std::move(view_holder_token1), presentation1.NewRequest());

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation2;
  bool alive2 = true;
  zx_status_t error = ZX_OK;
  presentation2.set_error_handler([&alive2, &error](zx_status_t err) {
    alive2 = false;
    error = err;
  });
  root_presenter()->PresentView(std::move(view_holder_token2), presentation2.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(alive1);
  EXPECT_FALSE(alive2);
  EXPECT_EQ(error, ZX_ERR_ALREADY_BOUND)
      << "Should be: " << zx_status_get_string(ZX_ERR_ALREADY_BOUND)
      << " Was: " << zx_status_get_string(error);
}

TEST_F(RootPresenterTest, SinglePresentOrReplaceView_ShouldSucceeed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation;
  bool alive = true;
  presentation.set_error_handler([&alive](auto) { alive = false; });
  root_presenter()->PresentView(std::move(view_holder_token), presentation.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(alive);
}

TEST_F(RootPresenterTest, SecondPresentOrReplaceView_ShouldSucceeed_AndOriginalShouldDie) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation1;
  bool alive1 = true;
  zx_status_t error = ZX_OK;
  presentation1.set_error_handler([&alive1, &error](zx_status_t err) {
    alive1 = false;
    error = err;
  });
  root_presenter()->PresentOrReplaceView(std::move(view_holder_token1), presentation1.NewRequest());

  fidl::InterfacePtr<fuchsia::ui::policy::Presentation> presentation2;
  bool alive2 = true;
  presentation2.set_error_handler([&alive2](auto) { alive2 = false; });
  root_presenter()->PresentOrReplaceView(std::move(view_holder_token2), presentation2.NewRequest());

  RunLoopUntilIdle();

  EXPECT_FALSE(alive1);
  EXPECT_EQ(error, ZX_ERR_PEER_CLOSED) << "Should be: " << zx_status_get_string(ZX_ERR_PEER_CLOSED)
                                       << " Was: " << zx_status_get_string(error);
  EXPECT_TRUE(alive2);
}

}  // namespace
}  // namespace root_presenter
