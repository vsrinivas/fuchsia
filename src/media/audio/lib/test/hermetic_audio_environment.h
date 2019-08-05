// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {

class HermeticAudioEnvironment {
 public:
  HermeticAudioEnvironment();
  ~HermeticAudioEnvironment();

  void Start(async::Loop* loop);

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

 private:
  // We use a mutex/condition variable allow the |hermetic_environment_| to be started on a
  // secondary thread. The majority of the functionality in |EnclosingEnvirionment| is just thin
  // wrappers around different FIDL calls so we don't guard all interaction with
  // |hermetic_environment_| with the mutex.
  std::condition_variable cv_;
  std::mutex mutex_;

  std::unique_ptr<sys::testing::EnclosingEnvironment> hermetic_environment_;
  std::thread env_thread_;
  async::Loop* loop_ = nullptr;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_ENVIRONMENT_H_
