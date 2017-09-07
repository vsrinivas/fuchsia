// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/media/src/audio_policy_service/audio_policy_service_impl.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  media::AudioPolicyServiceImpl impl(app::ApplicationContext::CreateFromStartupInfo());

  loop.Run();
  return 0;
}
