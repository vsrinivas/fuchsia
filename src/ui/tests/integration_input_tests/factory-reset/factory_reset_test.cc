// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/sounds/cpp/fidl.h>
#include <fuchsia/recovery/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/utc.h>

#include <gtest/gtest.h>

// This test exercises the factory reset path by injecting an input report into a real
// Root Presenter and asserting signals are received in a fake sound player and fake
// factory reset component.
//
// Factory reset dispatch path
// - Test program's injection -> Root Presenter -> Sound Player -> Test Assert
//                                              -> Factory Reset -> Test Assert

constexpr zx::duration kTimeout = zx::min(1);

// Common services for each test.
const std::map<std::string, std::string> LocalServices() {
  return {
      {"fuchsia.ui.input.InputDeviceRegistry",
       "fuchsia-pkg://fuchsia.com/factory-reset-test#meta/root_presenter.cmx"},
      {"fuchsia.ui.policy.Presenter",
       "fuchsia-pkg://fuchsia.com/factory-reset-test#meta/root_presenter.cmx"},
      // Scenic protocols.
      {"fuchsia.ui.scenic.Scenic", "fuchsia-pkg://fuchsia.com/factory-reset-test#meta/scenic.cmx"},
      {"fuchsia.ui.pointerinjector.Registry",
       "fuchsia-pkg://fuchsia.com/factory-reset-test#meta/scenic.cmx"},
      // Misc protocols.
      {"fuchsia.cobalt.LoggerFactory",
       "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cmx"},
      {"fuchsia.hardware.display.Provider",
       "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"},
  };
}

// Allow these global services from outside the test environment.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator",
          "fuchsia.scheduler.ProfileProvider"};
}

// A sound player used to fake a reset tone triggered by root presenter.
class SoundsPlayerImpl : public fuchsia::media::sounds::Player {
 public:
  explicit SoundsPlayerImpl(fidl::InterfaceRequest<fuchsia::media::sounds::Player> request) {}

  bool sound_played() { return sound_played_; }

  fidl::InterfaceRequestHandler<fuchsia::media::sounds::Player> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // |fuchsia::media::sounds::Player|
  void AddSoundFromFile(uint32_t id, fidl::InterfaceHandle<class fuchsia::io::File> file,
                        AddSoundFromFileCallback callback) override {
    callback(fuchsia::media::sounds::Player_AddSoundFromFile_Result::WithResponse(
        fuchsia::media::sounds::Player_AddSoundFromFile_Response((zx::sec(1), ZX_OK))));
  }

  // |fuchsia::media::sounds::Player|
  void PlaySound(uint32_t id, fuchsia::media::AudioRenderUsage usage,
                 PlaySoundCallback callback) override {
    sound_played_ = true;
    callback(fuchsia::media::sounds::Player_PlaySound_Result::WithResponse(
        fuchsia::media::sounds::Player_PlaySound_Response(ZX_OK)));
  }

 private:
  // |fuchsia::media::sounds::Player|
  void AddSoundBuffer(uint32_t id, fuchsia::mem::Buffer buffer,
                      fuchsia::media::AudioStreamType stream_type) override {
    FX_NOTIMPLEMENTED();
  }

  // |fuchsia::media::sounds::Player|
  void RemoveSound(uint32_t id) override { FX_NOTIMPLEMENTED(); }

  // |fuchsia::media::sounds::Player|
  void StopPlayingSound(uint32_t id) override { FX_NOTIMPLEMENTED(); }

  fidl::BindingSet<fuchsia::media::sounds::Player> bindings_;
  bool sound_played_{false};
};

// A fake factory reset component used to check that a reset signal was sent by root presenter.
class FactoryResetImpl : public fuchsia::recovery::FactoryReset {
 public:
  explicit FactoryResetImpl(fidl::InterfaceRequest<fuchsia::recovery::FactoryReset> request) {}

  bool factory_reset_triggered() { return factory_reset_triggered_; }

  fidl::InterfaceRequestHandler<fuchsia::recovery::FactoryReset> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // |fuchsia::recovery::FactoryReset|
  void Reset(ResetCallback callback) override { factory_reset_triggered_ = true; }

 private:
  fidl::BindingSet<fuchsia::recovery::FactoryReset> bindings_;
  bool factory_reset_triggered_{false};
};

class FactoryResetTest : public gtest::TestWithEnvironmentFixture {
 protected:
  explicit FactoryResetTest() {
    // Setup fake sound player and fake factory reset.
    fuchsia::media::sounds::PlayerPtr sound_player_ptr;
    sounds_player_ = std::make_unique<SoundsPlayerImpl>(sound_player_ptr.NewRequest());

    fuchsia::recovery::FactoryResetPtr factory_reset_ptr;
    factory_reset_ = std::make_unique<FactoryResetImpl>(factory_reset_ptr.NewRequest());

    auto services = sys::testing::EnvironmentServices::Create(real_env());
    zx_status_t is_ok;

    is_ok = services->AddService(sounds_player_->GetHandler());
    FX_CHECK(is_ok == ZX_OK);

    is_ok = services->AddService(factory_reset_->GetHandler());
    FX_CHECK(is_ok == ZX_OK);

    // Set up Root Presenter inside the test environment.
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    // Enable services from outside this test.
    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }

    test_env_ = CreateNewEnclosingEnvironment("factory_reset_test_env", std::move(services),
                                              {.inherit_parent_services = true});

    WaitForEnclosingEnvToStart(test_env_.get());

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);
  }

  ~FactoryResetTest() override {}

  // Inject directly into Root Presenter, using fuchsia.ui.input FIDLs.
  void InjectInput() {
    // Register an input device against Root Presenter.
    auto descriptor = fuchsia::ui::input::MediaButtonsDescriptor::New();
    *descriptor = {.buttons = 6};

    fuchsia::ui::input::DeviceDescriptor device{.media_buttons = std::move(descriptor)};
    auto registry = test_env_->ConnectToService<fuchsia::ui::input::InputDeviceRegistry>();
    fuchsia::ui::input::InputDevicePtr input_device;
    registry->RegisterDevice(std::move(device), input_device.NewRequest());
    FX_LOGS(INFO) << "Registered media buttons input device.";

    // Inject one media buttons input report, with only reset set.
    auto media_buttons_report = fuchsia::ui::input::MediaButtonsReport::New();
    *media_buttons_report = {
        .reset = true,
    };

    uint64_t injection_time = static_cast<uint64_t>(zx::clock::get_monotonic().get());
    fuchsia::ui::input::InputReport report{.event_time = injection_time,
                                           .media_buttons = std::move(media_buttons_report)};
    input_device->DispatchReport(fidl::Clone(report));
    FX_LOGS(INFO) << "Injected media buttons event for factory reset.";
  }

  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;
  std::unique_ptr<SoundsPlayerImpl> sounds_player_;
  std::unique_ptr<FactoryResetImpl> factory_reset_;
};

TEST_F(FactoryResetTest, FactoryReset) {
  InjectInput();

  FX_LOGS(INFO) << "Waiting for reset signal. This should should take about 10 seconds.";
  RunLoopUntil(
      [&] { return sounds_player_->sound_played() && factory_reset_->factory_reset_triggered(); });
}
