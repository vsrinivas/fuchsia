// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl_test_base.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl_test_base.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_interceptor.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"

namespace {

constexpr char kEnvironment[] = "present_view_integration_test";
constexpr char kPresentViewComponentUri[] =
    "fuchsia-pkg://fuchsia.com/present_view#meta/present_view.cmx";
constexpr char kNonexistentViewComponentUri[] = "file://nonexistent_view.cmx";
constexpr char kFakeViewComponentUri[] = "file://fake_view.cmx";
constexpr zx::duration kTimeout = zx::sec(1);

class FakePresentation : public fuchsia::ui::policy::testing::Presentation_TestBase {
 public:
  FakePresentation(fuchsia::ui::views::ViewHolderToken view_holder_token,
                   fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request)
      : binding_(this, std::move(presentation_request)),
        token_waiter_(std::make_unique<async::Wait>(
            view_holder_token.value.get(), __ZX_OBJECT_PEER_CLOSED, 0,
            std::bind([this]() { token_peer_disconnected_ = true; }))),
        token_(std::move(view_holder_token)) {}
  ~FakePresentation() override = default;
  FakePresentation(FakePresentation&& other) noexcept
      : binding_(this, other.binding_.Unbind()),
        token_waiter_(std::move(other.token_waiter_)),
        token_(std::move(other.token_)),
        token_peer_disconnected_(other.token_peer_disconnected_) {}

  const fuchsia::ui::views::ViewHolderToken& token() const { return token_; }
  bool peer_disconnected() const { return token_peer_disconnected_; }

  void NotImplemented_(const std::string& /*name*/) final { FAIL(); }

 private:
  fidl::Binding<fuchsia::ui::policy::Presentation> binding_;
  std::unique_ptr<async::Wait> token_waiter_;

  fuchsia::ui::views::ViewHolderToken token_;
  bool token_peer_disconnected_ = false;
};

class FakePresenter : public fuchsia::ui::policy::testing::Presenter_TestBase {
 public:
  FakePresenter() : binding_(this) {}
  ~FakePresenter() override = default;

  const std::vector<FakePresentation>& presentations() const { return presentations_; }
  bool bound() const { return binding_.is_bound(); }
  fidl::InterfaceRequestHandler<fuchsia::ui::policy::Presenter> handler() {
    return [this](fidl::InterfaceRequest<fuchsia::ui::policy::Presenter> request) {
      EXPECT_FALSE(bound());
      binding_.Bind(std::move(request));
    };
  }

  // |fuchsia::ui::policy::Presenter|
  void PresentView(
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) final {
    presentations_.emplace_back(std::move(view_holder_token), std::move(presentation_request));
  }
  void NotImplemented_(const std::string& /*name*/) final { FAIL(); }

 private:
  fidl::Binding<fuchsia::ui::policy::Presenter> binding_;

  std::vector<FakePresentation> presentations_;
};

class FakeViewComponent : public fuchsia::ui::app::testing::ViewProvider_TestBase {
 public:
  FakeViewComponent(fuchsia::sys::StartupInfo startup_info,
                    std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component)
      : component_context_(sys::ServiceDirectory::CreateFromNamespace(),
                           std::move(startup_info.launch_info.directory_request)),
        intercepted_component_(std::move(intercepted_component)),
        binding_(this) {
    component_context_.outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          EXPECT_FALSE(bound());
          binding_.Bind(std::move(request));
        });
    intercepted_component_->set_on_kill([this]() {
      token_ = fuchsia::ui::views::ViewToken();
      killed_ = true;
    });
  }

  ~FakeViewComponent() override {
    component_context_.outgoing()->RemovePublicService<fuchsia::ui::app::ViewProvider>();
  }

  bool bound() const { return binding_.is_bound(); }
  const fuchsia::ui::views::ViewToken& token() const { return token_; }
  bool peer_disconnected() const { return token_peer_disconnected_; }
  bool killed() const { return killed_; }

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) final {
    // Wait on the passed-in |ViewToken| so we can detect if the peer token is destroyed.
    // TODO(fxbug.dev/24197): Follow up on __ZX_OBJECT_PEER_CLOSED with Zircon.
    token_waiter_ =
        std::make_unique<async::Wait>(view_token.get(), __ZX_OBJECT_PEER_CLOSED, 0,
                                      std::bind([this]() { token_peer_disconnected_ = true; }));

    token_.value = std::move(view_token);
  }
  void NotImplemented_(const std::string& /*name*/) final { FAIL(); }

 private:
  sys::ComponentContext component_context_;
  std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component_;
  fidl::Binding<fuchsia::ui::app::ViewProvider> binding_;
  std::unique_ptr<async::Wait> token_waiter_;

  fuchsia::ui::views::ViewToken token_;
  bool token_peer_disconnected_ = false;
  bool killed_ = false;
};  // namespace

}  // namespace

namespace present_view::test {

// A test fixture which tests the full present_view component using a hermetic |Environment|.
class PresentViewComponentTest : public sys::testing::TestWithEnvironment {
 protected:
  PresentViewComponentTest()
      : interceptor_(sys::testing::ComponentInterceptor::CreateWithEnvironmentLoader(real_env())) {
    // We want to inject our fake components and services into the environment.
    auto services = interceptor_.MakeEnvironmentServices(real_env());
    services->AddService(fake_presenter_.handler());
    EXPECT_TRUE(interceptor_.InterceptURL(
        kNonexistentViewComponentUri, "",
        [](fuchsia::sys::StartupInfo /*startup_info*/,
           std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component) {
          // Simulate a failure to find the package.
          intercepted_component->Exit(-1, fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND);
        }));
    EXPECT_TRUE(interceptor_.InterceptURL(
        kFakeViewComponentUri, "",
        [this](fuchsia::sys::StartupInfo startup_info,
               std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component) {
          fake_view_component_ = std::make_unique<FakeViewComponent>(
              std::move(startup_info), std::move(intercepted_component));
        }));

    // Create the environment used in the test.
    environment_ = CreateNewEnclosingEnvironment(kEnvironment, std::move(services));
    WaitForEnclosingEnvToStart(environment_.get());

    // Reset status flags.
    present_view_channel_closed_ = false;
    present_view_closed_status_ = ZX_OK;
    present_view_terminated_ = false;
    present_view_return_code_ = 0;
    present_view_termination_reason_ = fuchsia::sys::TerminationReason::UNKNOWN;
  }

  void LaunchPresentViewComponentAndWait(std::vector<std::string> args, zx::duration timeout) {
    fuchsia::sys::LaunchInfo present_view_info{
        .url = kPresentViewComponentUri,
        .arguments = fidl::VectorPtr{std::move(args)},
    };

    // Reset status flags.
    present_view_channel_closed_ = false;
    present_view_closed_status_ = ZX_OK;
    present_view_terminated_ = false;
    present_view_return_code_ = 0;
    present_view_termination_reason_ = fuchsia::sys::TerminationReason::UNKNOWN;

    // Launch present_view in the hermetic environment.
    present_view_ = environment_->CreateComponent(std::move(present_view_info));
    present_view_.events().OnTerminated =
        [this](int64_t return_code, fuchsia::sys::TerminationReason termination_reason) {
          present_view_terminated_ = true;
          present_view_return_code_ = return_code;
          present_view_termination_reason_ = termination_reason;
        };
    present_view_.set_error_handler([this](zx_status_t status) {
      present_view_channel_closed_ = true;
      present_view_closed_status_ = status;
      QuitLoop();
    });

    RunLoopWithTimeout(timeout);
  }

  void KillPresentViewComponentAndWait(zx::duration timeout) {
    present_view_->Kill();

    RunLoopWithTimeout(timeout);
  }

  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  sys::testing::ComponentInterceptor interceptor_;

  std::unique_ptr<FakeViewComponent> fake_view_component_;
  FakePresenter fake_presenter_;

  fuchsia::sys::ComponentControllerPtr present_view_;
  fuchsia::sys::TerminationReason present_view_termination_reason_;
  zx_status_t present_view_closed_status_;
  bool present_view_channel_closed_;
  bool present_view_terminated_;
  int64_t present_view_return_code_;
};

TEST_F(PresentViewComponentTest, DISABLED_NoParams) {
  // Passing no parameters is invalid.
  //
  // present_view should fail, and never create a token pair.
  LaunchPresentViewComponentAndWait({}, kTimeout);
  EXPECT_FALSE(fake_view_component_);
  EXPECT_EQ(0u, fake_presenter_.presentations().size());
  EXPECT_TRUE(present_view_channel_closed_);
  EXPECT_TRUE(present_view_terminated_);
  EXPECT_EQ(1, present_view_return_code_);
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, present_view_termination_reason_);

  // Passing no *positional* parameters is invalid, even with valid options
  // passed.
  //
  // present_view should fail, and never create a token pair.
  LaunchPresentViewComponentAndWait(std::vector{std::string{"--verbose=0"}}, kTimeout);
  EXPECT_FALSE(fake_view_component_);
  EXPECT_EQ(0u, fake_presenter_.presentations().size());
  EXPECT_TRUE(present_view_channel_closed_);
  EXPECT_TRUE(present_view_terminated_);
  EXPECT_EQ(1, present_view_return_code_);
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, present_view_termination_reason_);
}

TEST_F(PresentViewComponentTest, DISABLED_InvalidComponentURI) {
  // Bad component URIs are invalid and cause present_view to fail.
  //
  // present_view should create a token pair and pass one end to |Presenter|,
  // but terminate itself once the specified component fails to launch.
  //
  LaunchPresentViewComponentAndWait(std::vector{std::string{kNonexistentViewComponentUri}},
                                    kTimeout);
  EXPECT_FALSE(fake_view_component_);
  EXPECT_EQ(1u, fake_presenter_.presentations().size());
  EXPECT_TRUE(fake_presenter_.presentations()[0].token().value);
  EXPECT_FALSE(fake_presenter_.presentations()[0].peer_disconnected());
  EXPECT_TRUE(present_view_channel_closed_);
  EXPECT_TRUE(present_view_terminated_);
  EXPECT_EQ(1u, present_view_return_code_);
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, present_view_termination_reason_);
}

TEST_F(PresentViewComponentTest, DISABLED_LaunchAndKillComponent) {
  // present_view should create a token pair and launch the specified component,
  // passing one end to |Presenter| and the other end to a |ViewProvider| from
  // the component.
  LaunchPresentViewComponentAndWait(std::vector{std::string{kFakeViewComponentUri}}, kTimeout);
  EXPECT_TRUE(fake_view_component_);
  EXPECT_FALSE(fake_view_component_->killed());
  EXPECT_EQ(1u, fake_presenter_.presentations().size());
  EXPECT_FALSE(present_view_channel_closed_);
  EXPECT_FALSE(present_view_terminated_);

  auto& view1_token = fake_view_component_->token();
  auto& view1_holder_token = fake_presenter_.presentations()[0].token();
  bool view1_disconnected = fake_presenter_.presentations()[0].peer_disconnected();
  bool view1_holder_disconnected = fake_view_component_->peer_disconnected();
  EXPECT_TRUE(view1_token.value);
  EXPECT_TRUE(view1_holder_token.value);
  EXPECT_FALSE(view1_disconnected);
  EXPECT_FALSE(view1_holder_disconnected);
  EXPECT_EQ(fsl::GetKoid(view1_token.value.get()),
            fsl::GetRelatedKoid(view1_holder_token.value.get()));
  EXPECT_EQ(fsl::GetKoid(view1_holder_token.value.get()),
            fsl::GetRelatedKoid(view1_token.value.get()));

  // Killing present_view will also kill the launched component.  The
  // |Presenter|'s token should get disconnected from its peer.
  KillPresentViewComponentAndWait(kTimeout);
  EXPECT_TRUE(fake_view_component_);
  EXPECT_TRUE(fake_view_component_->killed());
  EXPECT_EQ(1u, fake_presenter_.presentations().size());
  EXPECT_TRUE(present_view_channel_closed_);
  EXPECT_TRUE(present_view_terminated_);
  EXPECT_EQ(ZX_TASK_RETCODE_SYSCALL_KILL, present_view_return_code_);
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, present_view_termination_reason_);

  view1_disconnected = fake_presenter_.presentations()[0].peer_disconnected();
  view1_holder_disconnected = fake_view_component_->peer_disconnected();
  EXPECT_FALSE(view1_disconnected);
  EXPECT_FALSE(view1_holder_disconnected);

  // Launching present_view again after killing it should work.
  //
  // present_view should create a new token pair and launch the specified
  // component, as before.
  LaunchPresentViewComponentAndWait(std::vector{std::string{kFakeViewComponentUri}}, kTimeout);
  EXPECT_TRUE(fake_view_component_);
  EXPECT_FALSE(fake_view_component_->killed());
  EXPECT_EQ(2u, fake_presenter_.presentations().size());
  EXPECT_FALSE(present_view_channel_closed_);
  EXPECT_FALSE(present_view_terminated_);

  auto& view2_token = fake_view_component_->token();
  auto& view2_holder_token = fake_presenter_.presentations()[1].token();
  bool view2_disconnected = fake_presenter_.presentations()[1].peer_disconnected();
  bool view2_holder_disconnected = fake_view_component_->peer_disconnected();
  EXPECT_TRUE(view2_token.value);
  EXPECT_TRUE(view2_holder_token.value);
  EXPECT_FALSE(view2_disconnected);
  EXPECT_FALSE(view2_holder_disconnected);
  EXPECT_EQ(fsl::GetKoid(view2_token.value.get()),
            fsl::GetRelatedKoid(view2_holder_token.value.get()));
  EXPECT_EQ(fsl::GetKoid(view2_holder_token.value.get()),
            fsl::GetRelatedKoid(view2_token.value.get()));
}

}  // namespace present_view::test
