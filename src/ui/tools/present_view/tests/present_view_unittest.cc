// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/present_view/present_view.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl_test_base.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl_test_base.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>
#include <lib/sys/cpp/testing/fake_launcher.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/lib/fsl/handles/object_info.h"

namespace {

constexpr char kNonexistentViewComponentUrl[] = "file://nonexistent_view.cmx";
constexpr char kFakeViewComponentUrl[] = "file://fake_view.cmx";
constexpr zx::duration kTimeout = zx::sec(1);

class FakePresentation : public fuchsia::ui::policy::testing::Presentation_TestBase {
 public:
  FakePresentation(fuchsia::ui::views::ViewHolderToken view_holder_token,
                   fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request)
      : binding_(this, std::move(presentation_request)),
        token_waiter_(std::make_unique<async::Wait>(view_holder_token.value.get(),
                                                    __ZX_OBJECT_PEER_CLOSED, 0, std::bind([this]() {
                                                      FAIL();
                                                      token_peer_disconnected_ = true;
                                                    }))),
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
  explicit FakeViewComponent(sys::testing::FakeLauncher& fake_launcher) : binding_(this) {
    component_.Register(kFakeViewComponentUrl, fake_launcher);
    component_.AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          binding_.Bind(std::move(request));
        });
  }

  bool bound() const { return binding_.is_bound(); }
  const fuchsia::ui::views::ViewToken& token() const { return token_; }
  bool peer_disconnected() const { return token_peer_disconnected_; }

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) final {
    // Wait on the passed-in |ViewToken| so we can detect if the peer token is destroyed.
    token_waiter_ =
        std::make_unique<async::Wait>(view_token.get(), __ZX_OBJECT_PEER_CLOSED, 0,
                                      std::bind([this]() { token_peer_disconnected_ = true; }));

    token_.value = std::move(view_token);
  }
  void NotImplemented_(const std::string& /*name*/) final { FAIL(); }

 private:
  sys::testing::FakeComponent component_;
  fidl::Binding<fuchsia::ui::app::ViewProvider> binding_;
  std::unique_ptr<async::Wait> token_waiter_;

  fuchsia::ui::views::ViewToken token_;
  bool token_peer_disconnected_ = false;
};

}  // namespace

namespace present_view::test {

class PresentViewTest : public gtest::TestLoopFixture {
 protected:
  PresentViewTest()
      : fake_view_component_(fake_launcher_),
        present_view_app_(fake_context_provider_.TakeContext()) {
    fake_context_provider_.service_directory_provider()->AddService(fake_launcher_.GetHandler());
    fake_context_provider_.service_directory_provider()->AddService(fake_presenter_.handler());
  }

  zx_status_t LaunchPresentViewComponentAndWait(present_view::ViewInfo view_info) {
    zx_status_t present_status = ZX_OK;
    bool present_success = present_view_app_.Present(
        std::move(view_info), [&present_status](zx_status_t status) { present_status = status; });
    if (!present_success) {
      return ZX_ERR_INTERNAL;
    }

    RunLoopFor(kTimeout);

    return present_status;
  }

  sys::testing::ComponentContextProvider fake_context_provider_;
  sys::testing::FakeLauncher fake_launcher_;
  FakePresenter fake_presenter_;
  FakeViewComponent fake_view_component_;

  present_view::PresentView present_view_app_;
};

TEST_F(PresentViewTest, NoUrl) {
  // Passing no params is invalid.
  //
  // present_view should fail immediately, and never create a token pair.
  EXPECT_EQ(ZX_ERR_INTERNAL, LaunchPresentViewComponentAndWait({}));
  EXPECT_FALSE(fake_view_component_.bound());
  EXPECT_FALSE(fake_presenter_.bound());
  EXPECT_EQ(0u, fake_presenter_.presentations().size());

  auto& view_token = fake_view_component_.token();
  EXPECT_FALSE(view_token.value);

  // Passing no url is invalid, even with valid options passed.
  //
  // present_view should fail immediately, and never create a token pair.
  EXPECT_EQ(ZX_ERR_INTERNAL, LaunchPresentViewComponentAndWait({
                                 .arguments = std::vector{std::string{"foo"}},
                             }));
  EXPECT_FALSE(fake_view_component_.bound());
  EXPECT_FALSE(fake_presenter_.bound());
  EXPECT_EQ(0u, fake_presenter_.presentations().size());

  auto& view_token2 = fake_view_component_.token();
  EXPECT_FALSE(view_token2.value);
}

TEST_F(PresentViewTest, InvalidUrl) {
  // Invalid url's cause present_view to fail asynchronously.
  //
  // present_view should bind to |Presenter|, but stop the loop with
  // |ZX_ERR_PEER_CLOSED| once the specified component fails to launch.
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, LaunchPresentViewComponentAndWait({
                                    .url = std::string{kNonexistentViewComponentUrl},
                                }));
  EXPECT_FALSE(fake_view_component_.bound());
  EXPECT_TRUE(fake_presenter_.bound());
  EXPECT_EQ(1u, fake_presenter_.presentations().size());

  auto& view_holder_token = fake_presenter_.presentations()[0].token();
  bool view_disconnected = fake_presenter_.presentations()[0].peer_disconnected();
  EXPECT_TRUE(view_holder_token.value);
  EXPECT_FALSE(view_disconnected);
}

TEST_F(PresentViewTest, Launch) {
  // present_view should create a token pair and launch the specified component,
  // passing one end to |Presenter| and the other end to a |ViewProvider| from
  // the component.
  EXPECT_EQ(ZX_OK, LaunchPresentViewComponentAndWait({
                       .url = std::string{kFakeViewComponentUrl},
                   }));
  EXPECT_TRUE(fake_view_component_.bound());
  EXPECT_TRUE(fake_presenter_.bound());
  EXPECT_EQ(1u, fake_presenter_.presentations().size());

  auto& view_token = fake_view_component_.token();
  auto& view_holder_token = fake_presenter_.presentations()[0].token();
  bool view_disconnected = fake_presenter_.presentations()[0].peer_disconnected();
  bool view_holder_disconnected = fake_view_component_.peer_disconnected();
  EXPECT_TRUE(view_token.value);
  EXPECT_TRUE(view_holder_token.value);
  EXPECT_FALSE(view_disconnected);
  EXPECT_FALSE(view_holder_disconnected);
  EXPECT_EQ(fsl::GetKoid(view_token.value.get()),
            fsl::GetRelatedKoid(view_holder_token.value.get()));
  EXPECT_EQ(fsl::GetKoid(view_holder_token.value.get()),
            fsl::GetRelatedKoid(view_token.value.get()));
}

}  // namespace present_view::test
