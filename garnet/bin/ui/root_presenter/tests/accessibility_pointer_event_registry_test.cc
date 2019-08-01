// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/policy/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "garnet/bin/ui/root_presenter/app.h"

namespace root_presenter {
namespace testing {

// Simple fake that accepts calls to register an accessibility listener.
class FakePointerEventRegistry : public fuchsia::ui::policy::accessibility::PointerEventRegistry {
 public:
  FakePointerEventRegistry() = default;
  ~FakePointerEventRegistry() = default;

  // Returns a request handler for binding to this fake service.
  fidl::InterfaceRequestHandler<fuchsia::ui::policy::accessibility::PointerEventRegistry>
  GetRequestHandler() {
    return bindings_.GetHandler(this);
  }

  // |fuchsia.ui.policy.accessibility.PointerEventRegistry|
  // Records in |registered_| when this is called so that later it can be
  // checked in tests.
  void Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                    pointer_event_listener,
                RegisterCallback callback) override {
    fuchsia::ui::input::accessibility::PointerEventListenerPtr listener;
    listener.Bind(std::move(pointer_event_listener));
    registered_ = true;

    // Sends a dummy pointer event to the listener.
    listener->OnEvent(fuchsia::ui::input::accessibility::PointerEvent(),
                      [](uint32_t, uint32_t, fuchsia::ui::input::accessibility::EventHandling) {});
  }

  bool IsListenerRegistered() const { return registered_; }

 private:
  fidl::BindingSet<fuchsia::ui::policy::accessibility::PointerEventRegistry> bindings_;
  fuchsia::ui::input::accessibility::PointerEventListenerPtr accessibility_pointer_event_listener_;

  bool registered_ = false;
};

// Fake accessibility listener.
class FakePointerEventListener : public fuchsia::ui::input::accessibility::PointerEventListener {
 public:
  FakePointerEventListener() = default;
  ~FakePointerEventListener() = default;

  fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener> GetHandle() {
    return bindings_.AddBinding(this);
  }

  bool ReceivedEvent() const { return received_event_; }

 private:
  // |fuchsia::ui::input::accessibility::AccessibilityPointerEventListener|
  // This method records in |received_event_| when this fake is called, so it
  // can be later checked in tests.
  void OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event,
               OnEventCallback callback) override {
    received_event_ = true;
  }

  bool received_event_ = false;
  fidl::BindingSet<fuchsia::ui::input::accessibility::PointerEventListener> bindings_;
};

class AccessibilityPointerEventRegistryTest : public sys::testing::TestWithEnvironment {
 protected:
  void SetUp() override {
    auto services = CreateServices();

    // Add the service under test using its launch info.
    // Here, Root Presenter will have the interface
    // fuchsia::ui::input::accessibility::PointerEventRegistry tested.
    fuchsia::sys::LaunchInfo launch_info{
        "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"};
    zx_status_t status = services->AddServiceWithLaunchInfo(
        std::move(launch_info), fuchsia::ui::input::accessibility::PointerEventRegistry::Name_);
    EXPECT_EQ(ZX_OK, status);

    // Root Presenter calls another PointerEventRegistry, this time, in
    // fuchsia::ui::policy::accessibility.
    services->AddService(fake_pointer_event_registry_.GetRequestHandler(),
                         fuchsia::ui::policy::accessibility::PointerEventRegistry::Name_);

    // Create the synthetic environment.
    environment_ =
        CreateNewEnclosingEnvironment("accessibility_pointer_event_registry", std::move(services));
    WaitForEnclosingEnvToStart(environment_.get());

    // Instantiate the registry. This is the interface being tested.
    environment_->ConnectToService(registry_.NewRequest());
    ASSERT_TRUE(registry_.is_bound());
  }

  fuchsia::ui::input::accessibility::PointerEventRegistryPtr registry_;

  FakePointerEventRegistry fake_pointer_event_registry_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
};

TEST_F(AccessibilityPointerEventRegistryTest, Registers) {
  FakePointerEventListener fake_listener;
  auto listener_handle = fake_listener.GetHandle();
  registry_->Register(std::move(listener_handle));
  RunLoopUntil([&fake_listener] { return fake_listener.ReceivedEvent(); });
  EXPECT_TRUE(fake_pointer_event_registry_.IsListenerRegistered());
}

}  // namespace testing
}  // namespace root_presenter
