// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/views/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/bin/root_presenter/app.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_scenic.h"

namespace root_presenter {
namespace testing {

class AccessibilityFocuserRegistryTest : public sys::testing::TestWithEnvironment {
 protected:
  void SetUp() override {
    auto services = CreateServices();

    // Add the service under test using its launch info.
    // Here, Root Presenter will have the interface
    // fuchsia::ui::views::accessibility::FocuserRegistry tested.
    // The component is a singleton. This means that the same Root Presenter will handle the two
    // services added here.
    zx_status_t status = services->AddServiceWithLaunchInfo(
        {.url = "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"},
        fuchsia::ui::views::accessibility::FocuserRegistry::Name_);
    ASSERT_EQ(ZX_OK, status);

    status = services->AddServiceWithLaunchInfo(
        {.url = "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"},
        fuchsia::ui::policy::Presenter::Name_);
    ASSERT_EQ(ZX_OK, status);

    services->AddService(fake_scenic_.GetHandler(), fuchsia::ui::scenic::Scenic::Name_);

    // Create the synthetic environment.
    environment_ =
        CreateNewEnclosingEnvironment("accessibility_focuser_registry", std::move(services));
    WaitForEnclosingEnvToStart(environment_.get());

    // Instantiate the registry. This is the interface being tested.
    environment_->ConnectToService(registry_.NewRequest());
    // Instantiate the presenter_. This is a helper interface to initialize Scenic services inside
    // Root Presenter.
    environment_->ConnectToService(presenter_.NewRequest());

    ASSERT_TRUE(registry_.is_bound());
    ASSERT_TRUE(presenter_.is_bound());
  }

  fuchsia::ui::views::accessibility::FocuserRegistryPtr registry_;
  fuchsia::ui::policy::PresenterPtr presenter_;

  FakeScenic fake_scenic_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
};

TEST_F(AccessibilityFocuserRegistryTest, AccessibilityFocusRequestFailsWhenScenicIsNotInitialized) {
  fuchsia::ui::views::FocuserPtr view_focuser;
  registry_->RegisterFocuser(view_focuser.NewRequest());
  RunLoopUntil([&view_focuser] { return view_focuser.is_bound(); });
  bool callback_ran = false;
  auto callback = [&callback_ran](fuchsia::ui::views::Focuser_RequestFocus_Result result) {
    EXPECT_TRUE(result.is_err());
    callback_ran = true;
  };
  auto [view_control_ref, view_ref] = scenic::ViewRefPair::New();
  view_focuser->RequestFocus(std::move(view_ref), std::move(callback));
  RunLoopUntil([&callback_ran] { return callback_ran; });
}

TEST_F(AccessibilityFocuserRegistryTest, AccessibilityFocusRequestIsForwardedToScenic) {
  fuchsia::ui::views::FocuserPtr view_focuser;
  registry_->RegisterFocuser(view_focuser.NewRequest());
  RunLoopUntil([&view_focuser] { return view_focuser.is_bound(); });
  bool callback_ran = false;
  auto callback = [&callback_ran](fuchsia::ui::views::Focuser_RequestFocus_Result result) {
    EXPECT_FALSE(result.is_err());
    callback_ran = true;
  };
  // Here, a dummy call to PresentView() is done so that Scenic services are initialized.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  presenter_->PresentView(std::move(view_holder_token), nullptr);
  RunLoopUntilIdle();
  auto [view_control_ref, view_ref] = scenic::ViewRefPair::New();
  view_focuser->RequestFocus(std::move(view_ref), std::move(callback));
  RunLoopUntil([&callback_ran] { return callback_ran; });
}

}  // namespace testing
}  // namespace root_presenter
