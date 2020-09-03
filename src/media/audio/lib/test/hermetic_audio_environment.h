// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/cpp/vmo/snapshot.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {

class HermeticAudioEnvironment {
 public:
  struct Options {
    std::string audio_core_base_url = "fuchsia-pkg://fuchsia.com/audio-core-for-test";
    std::string audio_core_config_data_path = "";
    std::vector<std::string> audio_core_arguments;
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

  // Components started by this environment.
  enum ComponentType {
    kAudioComponent,
    kAudioCoreComponent,
    kVirtualAudioComponent,
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

  // This field must be locked during the constructor and StartEnvThread, but after the constructor
  // is complete, the field is read only and can be accessed without locking.
  std::unique_ptr<sys::testing::EnclosingEnvironment> hermetic_environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
  std::shared_ptr<sys::ServiceDirectory> devmgr_services_;

  // Locking not needed to access these fields: they are initialized during single-threaded
  // setup code within the constructor.
  async::Loop* loop_ = nullptr;
  std::unordered_map<ComponentType, std::string> component_urls_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_
