// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <vector>

#include "src/lib/syslog/cpp/logger.h"

namespace accessibility_test {

namespace {

constexpr zx::duration kTimeout = zx::sec(60);

// Helper class to retrieve a view's koid which can be used to construct its ViewRef.
//
// Implements a |fuchsia::ui::input::accessibility::PointerEventListener| that
// gets called for all pointer events. It extracts the view's koid from the event
// while consuming them.
class KoidListener : public fuchsia::ui::input::accessibility::PointerEventListener {
 public:
  void set_events(fuchsia::ui::input::accessibility::PointerEventListener::EventSender_* events) {
    events_ = events;
  }

  zx_koid_t koid() const { return koid_; }

 private:
  // |fuchsia::ui::input::accessibility::PointerEventListener|
  void OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event) override {
    if (pointer_event.has_viewref_koid()) {
      koid_ = pointer_event.viewref_koid();
    }

    // Consume everything.
    FX_CHECK(pointer_event.has_phase());
    if (pointer_event.phase() == fuchsia::ui::input::PointerEventPhase::ADD) {
      FX_CHECK(pointer_event.has_device_id());
      FX_CHECK(pointer_event.has_pointer_id());
      events_->OnStreamHandled(pointer_event.device_id(), pointer_event.pointer_id(),
                               fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
    }
  }

  fuchsia::ui::input::accessibility::PointerEventListener::EventSender_* events_ = nullptr;
  zx_koid_t koid_ = ZX_KOID_INVALID;
};

// Copied from web_runner_pixel_tests.cc:
//
// Invokes the input tool for input injection.
// See src/ui/tools/input/README.md or `input --help` for usage details.
// Commands used here:
//  * tap <x> <y> (scaled out of 1000)
void Input(std::vector<const char*> args) {
  // start with proc name, end with nullptr
  args.insert(args.begin(), "input");
  args.push_back(nullptr);

  zx_handle_t proc;
  zx_status_t status =
      fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/bin/input", args.data(), &proc);
  FX_CHECK(status == ZX_OK) << "fdio_spawn: " << zx_status_get_string(status);

  status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED,
                              (zx::clock::get_monotonic() + kTimeout).get(), nullptr);
  FX_CHECK(status == ZX_OK) << "zx_object_wait_one: " << zx_status_get_string(status);

  zx_info_process_t info;
  status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << "zx_object_get_info: " << zx_status_get_string(status);
  FX_CHECK(info.return_code == 0) << info.return_code;
}

}  // namespace

SemanticsIntegrationTest::SemanticsIntegrationTest(const std::string& environment_label)
    : environment_label_(environment_label),
      component_context_(sys::ComponentContext::Create()),
      view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                    component_context_->outgoing()->debug_dir()),
      scenic_(component_context_->svc()->Connect<fuchsia::ui::scenic::Scenic>()) {}

void SemanticsIntegrationTest::SetUp() {
  TestWithEnvironment::SetUp();
  // This is done in |SetUp| as opposed to the constructor to allow subclasses the opportunity to
  // override |CreateServices()|.
  auto services = TestWithEnvironment::CreateServices();
  services->AddService(semantics_manager_bindings_.GetHandler(&view_manager_));

  CreateServices(services);

  environment_ = CreateNewEnclosingEnvironment(environment_label_, std::move(services),
                                               {.inherit_parent_services = true});
}

fuchsia::ui::views::ViewToken SemanticsIntegrationTest::CreatePresentationViewToken() {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  auto presenter = real_services()->Connect<fuchsia::ui::policy::Presenter>();
  presenter.set_error_handler(
      [](zx_status_t status) { FAIL() << "presenter: " << zx_status_get_string(status); });
  presenter->PresentView(std::move(view_holder_token), nullptr);

  return std::move(view_token);
}

zx_koid_t SemanticsIntegrationTest::WaitForKoid() {
  // There are a few alternatives as to how to do this, of which tapping and intercepting is one.
  //
  // Another is to fake out Scenic. However,
  // * Chromium registers two views with Scenic, only one of which registers with accessibility.
  // * The dance between Scenic and runners can be pretty complex (e.g. initialization and
  //   dimensioning), any part of which might result in the runner bailing. It's fragile to try to
  //   fake this.
  //
  // Another is to add test-only methods to semantics manager, but this is also subject to the
  // initialization fidelity concerns unless a real Scenic is used anyway.
  // TODO(fxb/49011): Use ViewProvider::GetViewRef when available.

  auto pointer_event_registry =
      real_services()->Connect<fuchsia::ui::input::accessibility::PointerEventRegistry>();

  KoidListener listener;
  fidl::Binding<fuchsia::ui::input::accessibility::PointerEventListener> binding(&listener);
  listener.set_events(&binding.events());
  pointer_event_registry->Register(binding.NewBinding());
  // fxb/42959: This will fail the first few times.
  binding.set_error_handler([&](auto) { pointer_event_registry->Register(binding.NewBinding()); });

  const auto deadline = zx::clock::get_monotonic() + kTimeout;

  do {
    Input({"tap", "500", "500"});  // centered
    // The timing here between initializing, registering, rendering, and tapping can get pretty
    // messy without surefire ways of synchronizing, so rather than try to do anything clever let's
    // keep it simple.
    RunLoopWithTimeoutOrUntil([&listener] { return listener.koid() != ZX_KOID_INVALID; });
  } while (listener.koid() == ZX_KOID_INVALID && zx::clock::get_monotonic() < deadline);

  return listener.koid();
}

}  // namespace accessibility_test
