// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/focus_dispatcher.h"

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <vector>

#include <gtest/gtest.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/bin/root_presenter/focus_listener.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_keyboard_focus_controller.h"

namespace root_presenter {

using fuchsia::ui::focus::FocusChain;
using fuchsia::ui::focus::FocusChainListener;
using fuchsia::ui::focus::FocusChainListenerRegistry;
using fuchsia::ui::keyboard::focus::Controller;
using fuchsia::ui::views::ViewRef;

class FakeFocusListener : public FocusListener {
 public:
  FakeFocusListener(fit::function<void(ViewRef)> callback)
      : callback_(std::move(callback)), weak_ptr_factory_(this) {}
  void NotifyFocusChange(ViewRef focused_view) override { callback_(std::move(focused_view)); }
  fxl::WeakPtr<FakeFocusListener> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  fit::function<void(ViewRef)> callback_;
  fxl::WeakPtrFactory<FakeFocusListener> weak_ptr_factory_;  // Must be last.
};

class FocusDispatcherTest : public gtest::RealLoopFixture, public FocusChainListenerRegistry {
 public:
  FocusDispatcherTest()
      : focus_listener_(FakeFocusListener([this](ViewRef) { local_listener_notified_ = true; })) {}

  void SetUp() final {
    // Installs 'this' as a fake server for FocusChainListenerRegistry.
    context_provider_.service_directory_provider()->AddService(
        focus_listener_registry_.GetHandler(this));

    // Installs a fake receiver for keyboard focus events, and asks it to flip
    // a flag if a notification comes in.
    fake_keyboard_focus_controller_ =
        std::make_unique<testing::FakeKeyboardFocusController>(context_provider_);
    fake_keyboard_focus_controller_->SetOnNotify(
        [&](const ViewRef& view_ref) { keyboard_notification_received_ = true; });

    controller_handler_ = fake_keyboard_focus_controller_->GetHandler();

    // Finally, initializes the unit under test.
    focus_dispatch_ = std::make_unique<FocusDispatcher>(context_provider_.context()->svc(),
                                                        focus_listener_.GetWeakPtr());
  }

  // Implements `fuchsia.ui.focus.FocusChainListenerRegistry`, but only for a single
  // listener registration.
  void Register(fidl::InterfaceHandle<FocusChainListener> listener) override {
    ASSERT_EQ(ZX_OK, focus_chain_listener_.Bind(std::move(listener)));
    focus_chain_listener_.set_error_handler([](zx_status_t status) {
      FAIL() << "error while talking to focus chain listener: " << zx_status_get_string(status);
    });
    register_calls_++;
  }

  void SendFocusChain(FocusChain focus_chain) {
    focus_chain_listener_->OnFocusChange(std::move(focus_chain), [&] { focus_dispatched_++; });
  }

  void ChangeFocus(std::vector<ViewRef> view_refs) {
    FocusChain focus_chain;
    focus_chain.set_focus_chain(std::move(view_refs));
    SendFocusChain(std::move(focus_chain));
  }

  void SendEmptyFocus() {
    FocusChain focus_chain;
    SendFocusChain(std::move(focus_chain));
  }

  ViewRef MakeViewRef() {
    auto view_ref_pair = scenic::ViewRefPair::New();
    return fidl::Clone(view_ref_pair.view_ref);
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;

  fidl::BindingSet<fuchsia::ui::focus::FocusChainListenerRegistry> focus_listener_registry_;

  std::unique_ptr<testing::FakeKeyboardFocusController> fake_keyboard_focus_controller_;

  // The client-end connection to a test FocusChainListener.
  fidl::InterfacePtr<FocusChainListener> focus_chain_listener_;

  fidl::InterfaceRequestHandler<Controller> controller_handler_;

  FakeFocusListener focus_listener_;

  // Class under test.
  // Must not be re-ordered before `focus_listener_`, as the FocusDispatcher's
  // reference to `focus_listener_` must not out-live `focus_listener_`.
  std::unique_ptr<FocusDispatcher> focus_dispatch_;

  bool keyboard_notification_received_{};
  int focus_dispatched_{};
  int register_calls_{};
  bool local_listener_notified_{};
};

TEST_F(FocusDispatcherTest, Forward) {
  // Give the opportunity for Register(...) to get called.
  RunLoopUntilIdle();
  ASSERT_NE(0, register_calls_) << "FocusDispatcher should call Register";

  std::vector<ViewRef> v;
  v.emplace_back(MakeViewRef());
  ChangeFocus(std::move(v));

  RunLoopUntilIdle();
  EXPECT_NE(0, focus_dispatched_) << "ChangeFocus should have dispatched OnFocusChange";
  EXPECT_TRUE(keyboard_notification_received_);
  EXPECT_TRUE(local_listener_notified_);
}

TEST_F(FocusDispatcherTest, EmptyFocusChain) {
  RunLoopUntilIdle();
  ASSERT_NE(0, register_calls_) << "FocusDispatcher should call Register";

  ChangeFocus({});

  RunLoopUntilIdle();
  EXPECT_NE(0, focus_dispatched_) << "ChangeFocus should have dispatched OnFocusChange";

  // Nothing is called with an empty focus chain.
  EXPECT_FALSE(keyboard_notification_received_);
}

TEST_F(FocusDispatcherTest, UnsetFocusChain) {
  RunLoopUntilIdle();
  ASSERT_NE(0, register_calls_) << "FocusDispatcher should call Register";

  SendEmptyFocus();

  RunLoopUntilIdle();
  EXPECT_NE(0, focus_dispatched_) << "ChangeFocus should have dispatched OnFocusChange";

  // Nothing is called with an empty focus chain.
  EXPECT_FALSE(keyboard_notification_received_);
}

}  // namespace root_presenter
