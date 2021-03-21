// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/present_view/present_view.h"

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_launcher.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/tools/present_view/testing/fake_presenter.h"
#include "src/ui/tools/present_view/testing/fake_unittest_view.h"

namespace present_view::test {

// This test fixture tests |PresentView| logic on a single thread.
//
// The test fixture provides fake |fuchsia.ui.policy.Presenter| and |fuchsia.ui.app.ViewProvider|
// implementations and services them on its main loop.
//
// Each test also instantiates a |PresentView| object and services that object on its main loop.
class PresentViewTest : public gtest::TestLoopFixture {
 protected:
  PresentViewTest()
      : present_view_(fake_context_provider_.TakeContext(),
                      [this](std::string error_string, zx_status_t status) {
                        service_error_string_ = std::move(error_string);
                        service_status_ = status;
                      }),
        fake_view_(fake_launcher_) {
    fake_context_provider_.service_directory_provider()->AddService(fake_launcher_.GetHandler());
    fake_context_provider_.service_directory_provider()->AddService(fake_presenter_.GetHandler());
  }

  bool LaunchPresentViewComponentAndWait(present_view::ViewInfo view_info) {
    bool success = present_view_.Present(std::move(view_info));
    if (success) {
      success = success && RunLoopUntilIdle();
    }

    return success;
  }

  sys::testing::ComponentContextProvider fake_context_provider_;
  sys::testing::FakeLauncher fake_launcher_;

  present_view::PresentView present_view_;
  present_view::testing::FakePresenter fake_presenter_;
  present_view::testing::FakeUnitTestView fake_view_;

  std::string service_error_string_;
  zx_status_t service_status_ = ZX_OK;
};

TEST_F(PresentViewTest, NoUrl) {
  // Passing no params does nothing (but prints a warning).
  //
  // present_view should exit immediately without connecting to any services, and never create a
  // token pair.
  EXPECT_FALSE(LaunchPresentViewComponentAndWait({}));
  EXPECT_EQ(ZX_OK, service_status_);
  EXPECT_FALSE(fake_view_.bound());
  EXPECT_FALSE(fake_presenter_.bound());
  EXPECT_FALSE(fake_presenter_.presentation());

  auto& view_token = fake_view_.token();
  EXPECT_FALSE(view_token.value);

  // Passing no url does nothing (but prints a warning), even with valid options passed.
  //
  // present_view should exit immediately without connecting to any services, and never create a
  // token pair.
  EXPECT_FALSE(LaunchPresentViewComponentAndWait({
      .url = std::string{},
      .arguments = std::vector{std::string{"foo"}},
  }));
  EXPECT_EQ(ZX_OK, service_status_);
  EXPECT_FALSE(fake_view_.bound());
  EXPECT_FALSE(fake_presenter_.bound());
  EXPECT_FALSE(fake_presenter_.presentation());

  auto& view_token2 = fake_view_.token();
  EXPECT_FALSE(view_token2.value);
}

TEST_F(PresentViewTest, InvalidUrl) {
  // Invalid url's cause present_view to fail asynchronously.
  //
  // present_view should bind to |Presenter|, but stop the loop with |ZX_ERR_PEER_CLOSED| and unbind
  // from |Presenter| once the specified component fails to launch.
  EXPECT_TRUE(LaunchPresentViewComponentAndWait({
      .url = std::string{testing::kNonexistentViewUri},
  }));
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, service_status_);
  EXPECT_FALSE(fake_view_.bound());
  EXPECT_FALSE(fake_presenter_.bound());
  EXPECT_TRUE(fake_presenter_.presentation());
  EXPECT_TRUE(fake_presenter_.presentation()->peer_disconnected());

  auto& view_holder_token = fake_presenter_.presentation()->token();
  EXPECT_TRUE(view_holder_token.value);
}

TEST_F(PresentViewTest, Launch) {
  // present_view should create a token pair and launch the specified component, passing one end to
  // |Presenter| and the other end to a |ViewProvider| from the component.
  //
  // Once present_view is closed, the client View and the Presenter both keep running without any
  // need for `present_view`'s intervention.
  //
  // Once the client View closes, the Presenter gets a disconnect signal on its View token.
  EXPECT_TRUE(LaunchPresentViewComponentAndWait({
      .url = std::string{testing::kFakeViewUri},
  }));
  EXPECT_EQ(ZX_OK, service_status_);
  EXPECT_TRUE(fake_view_.bound());
  EXPECT_FALSE(fake_view_.peer_disconnected());
  EXPECT_TRUE(fake_presenter_.bound());
  EXPECT_TRUE(fake_presenter_.presentation());
  EXPECT_FALSE(fake_presenter_.presentation()->peer_disconnected());

  // Validate the Presenter and View's tokens came from the same eventpair.
  auto& view_token = fake_view_.token();
  auto& view_holder_token = fake_presenter_.presentation()->token();
  EXPECT_TRUE(view_token.value);
  EXPECT_TRUE(view_holder_token.value);
  EXPECT_EQ(fsl::GetKoid(view_token.value.get()),
            fsl::GetRelatedKoid(view_holder_token.value.get()));
  EXPECT_EQ(fsl::GetKoid(view_holder_token.value.get()),
            fsl::GetRelatedKoid(view_token.value.get()));

  // Kill present_view.
  // present_view will now disconnect from the token exchange interface for the client.
  // The Presenter and the client View tokens will remain linked.
  present_view_.Kill();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(fake_presenter_.bound());         // present_view disconnects from Presenter
  EXPECT_FALSE(fake_view_.bound());              // present_view disconnects from client View
  EXPECT_FALSE(fake_view_.peer_disconnected());  // Clients token still linked
  EXPECT_FALSE(
      fake_presenter_.presentation()->peer_disconnected());  // Presenters token still linked

  // Kill the fake View.
  // This will cause a peer_disconnect event in the Presenter.
  fake_view_.Kill();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(fake_view_.killed());              // Client forcibly killed
  EXPECT_FALSE(fake_view_.peer_disconnected());  // Client killed, so never disconnected
  EXPECT_FALSE(fake_view_.token().value);        // Client destroyed its token
  EXPECT_TRUE(
      fake_presenter_.presentation()->peer_disconnected());  // Presenters token disconnected
}

}  // namespace present_view::test
