// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/cpp/vmo/snapshot.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <zircon/types.h>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/audio/effects/test_effects/test_effects_v2.h"
#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {

class HermeticAudioEnvironment {
 public:
  struct Options {
    std::string audio_core_base_url = "fuchsia-pkg://fuchsia.com/audio-core-for-test";
    std::string audio_core_config_data_path = "";
    std::string devmgr_url =
        "fuchsia-pkg://fuchsia.com/audio-core-api-tests#meta/audio-test-devmgr.cmx";
    // clang-format off
    std::string virtual_audio_url =
        "fuchsia-pkg://fuchsia.com/virtual-audio-service-for-test#meta/virtual_audio_service_nodevfs.cmx";
    // clang-format on
    std::vector<std::string> audio_core_arguments;
    std::vector<std::string> extra_allowed_parent_services;
    std::vector<TestEffectsV2::Effect> test_effects_v2;
    std::string processor_creator_url;
    std::string processor_creator_config_data_path;
    std::function<zx_status_t(sys::testing::EnvironmentServices&)> install_additional_services_fn;
    std::string label = "hermetic_audio_test";
  };
  HermeticAudioEnvironment(Options options);
  ~HermeticAudioEnvironment();

  template <typename Interface>
  void ConnectToService(fidl::InterfaceRequest<Interface> request,
                        const std::string& service_name = Interface::Name_) {
    hermetic_environment_->ConnectToService(service_name, request.TakeChannel());
  }

  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToService(
      const std::string& service_name = Interface::Name_) {
    fidl::InterfacePtr<Interface> ptr;
    ConnectToService(ptr.NewRequest(), service_name);
    return ptr;
  }

  sys::testing::EnclosingEnvironment& GetEnvironment() const {
    FX_CHECK(hermetic_environment_ && hermetic_environment_->is_running());
    return *hermetic_environment_;
  }

  // Components started by this environment.
  enum ComponentType {
    kAudioCoreComponent,
    kVirtualAudioComponent,
    kThermalTestControlComponent,
    kProcessorCreatorComponent,
  };

  // Read the exported inspect info for the given component.
  const inspect::Hierarchy ReadInspect(ComponentType component_type);

 private:
  static void EnvironmentMain(HermeticAudioEnvironment* env);
  void StartEnvThread(async::Loop* loop);

  const Options options_;

  std::thread env_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;

  // These fields must be locked during the constructor and StartEnvThread, but after the
  // constructor is complete, the fields are read only and can be accessed without locking.
  std::unique_ptr<sys::testing::EnclosingEnvironment> hermetic_environment_;
  std::shared_ptr<sys::ServiceDirectory> devmgr_services_;

  // Locking not needed to access these fields: they are initialized during single-threaded
  // setup code within the constructor.
  async::Loop* loop_ = nullptr;
  std::unordered_map<ComponentType, std::string> component_urls_;
  fuchsia::sys::ComponentControllerPtr controller_;
  TestEffectsV2 test_effects_v2_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_
