// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/ui/base_view/base_view.h"

// NOTE WELL. Run each of these e2e tests in its own executable.  They each
// consume and maintain process-global context, so it's better to keep them
// separate.  Plus, separation means they start up components in a known good
// state, instead of reusing component state possibly dirtied by other tests.

namespace {

using fuchsia::ui::input::MediaButtonsEvent;

// Shared context for all tests in this process.
// Set it up once, never delete it.
sys::ComponentContext* g_context = nullptr;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// This implements the MediaButtonsListener class. Its purpose is to attach
// to the presentation and test that MediaButton Events are actually sent
// out to the Listeners.
class ButtonsListenerImpl : public fuchsia::ui::policy::MediaButtonsListener {
 public:
  ButtonsListenerImpl(
      fidl::InterfaceRequest<fuchsia::ui::policy::MediaButtonsListener> listener_request,
      fit::function<void(const MediaButtonsEvent&)> on_terminate)
      : listener_binding_(this, std::move(listener_request)),
        on_terminate_(std::move(on_terminate)) {}

 private:
  // |MediaButtonsListener|
  void OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) override {
    if (observed_count_ == 0) {
      // Terminate on first event.
      on_terminate_(event);
      ++observed_count_;
    }
  }

  fidl::Binding<fuchsia::ui::policy::MediaButtonsListener> listener_binding_;
  fit::function<void(const MediaButtonsEvent&)> on_terminate_;
  uint32_t observed_count_ = 0;
};

class MediaButtonsListenerTest : public gtest::RealLoopFixture {
 protected:
  MediaButtonsListenerTest() {
    // This fixture constructor may run multiple times, but we want the context
    // to be set up just once per process.
    if (g_context == nullptr) {
      g_context = sys::ComponentContext::CreateAndServeOutgoingDirectory().release();
    }
  }

  ~MediaButtonsListenerTest() override {
    FX_CHECK(injection_count_ == 1) << "Oops, didn't actually do anything.";
  }

  void InjectInput(std::vector<const char*> args) {
    // Start with process name, end with nullptr.
    args.insert(args.begin(), "input");
    args.push_back(nullptr);

    // Start the /bin/input process.
    zx_handle_t proc;
    zx_status_t status =
        fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/bin/input", args.data(), &proc);
    FX_CHECK(status == ZX_OK) << "fdio_spawn: " << zx_status_get_string(status);

    // Wait for termination.
    status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED,
                                (zx::clock::get_monotonic() + kTimeout).get(), nullptr);
    FX_CHECK(status == ZX_OK) << "zx_object_wait_one: " << zx_status_get_string(status);

    // Check termination status.
    zx_info_process_t info;
    status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    FX_CHECK(status == ZX_OK) << "zx_object_get_info: " << zx_status_get_string(status);
    FX_CHECK(info.return_code == 0) << "info.return_code: " << info.return_code;
  }

  std::unique_ptr<ButtonsListenerImpl> button_listener_impl_;
  fuchsia::ui::policy::DeviceListenerRegistryPtr root_presenter_;

  uint32_t injection_count_ = 0;
};

TEST_F(MediaButtonsListenerTest, MediaButtons) {
  // Post input injection in the future, "long enough" that the RegisterMediaButtonsListener will
  // have succeeded.
  // TODO(fxbug.dev/41384): Make this more reliable by parking a callback on a response for
  // RegisterMediaButtonsListener.
  async::PostDelayedTask(
      dispatcher(),
      [this] {
        // Set up inputs. Fires when display and content are available.
        // Inject a media button input with all buttons but the factory reset button
        // set. If fdr is set, FactoryResetManager will handle the buttons event
        // instead of the MediaButtonListener, which we are testing.
        InjectInput({"media_button", "1", "1", "1", "0", "1", nullptr});
        ++injection_count_;
      },
      zx::sec(1));

  // Set up expectations. Terminate when we see 1 message.
  fit::function<void(const MediaButtonsEvent&)> on_terminate =
      [this](const MediaButtonsEvent& observed) {
        ASSERT_TRUE(observed.has_mic_mute());
        EXPECT_TRUE(observed.mic_mute());

        ASSERT_TRUE(observed.has_volume());
        EXPECT_EQ(observed.volume(), 0);

        QuitLoop();
        // TODO(fxbug.dev/24638): Cleanly break the View/ViewHolder connection.
      };

  // Register the MediaButtons listener against Root Presenter.
  fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener_handle;
  button_listener_impl_ =
      std::make_unique<ButtonsListenerImpl>(listener_handle.NewRequest(), std::move(on_terminate));

  root_presenter_ = g_context->svc()->Connect<fuchsia::ui::policy::DeviceListenerRegistry>();
  root_presenter_.set_error_handler([](zx_status_t status) {
    FX_LOGS(FATAL) << "Lost connection to RootPresenter: " << zx_status_get_string(status);
  });
  root_presenter_->RegisterMediaButtonsListener(std::move(listener_handle));

  // Post a "just in case" quit task, if the test hangs.
  async::PostDelayedTask(
      dispatcher(),
      [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating. <<\n\n"; },
      kTimeout);

  RunLoop();  // Go!
}

}  // namespace

// NOTE: We link in FXL's gtest_main to enable proper logging.
