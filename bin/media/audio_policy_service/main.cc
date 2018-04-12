// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/media/audio_policy_service/audio_policy_service_impl.h"
#include "lib/app/cpp/application_context.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  audio_policy::AudioPolicyServiceImpl impl(
      component::ApplicationContext::CreateFromStartupInfo());

  loop.Run();
  return 0;
}
