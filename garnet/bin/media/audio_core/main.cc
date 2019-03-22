// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/media/audio_core/audio_core_impl.h"
#include "lib/sys/cpp/component_context.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  media::audio::AudioCoreImpl impl(
      sys::ComponentContext::Create());
  loop.Run();
  return 0;
}
