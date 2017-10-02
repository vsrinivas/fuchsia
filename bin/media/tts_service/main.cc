// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/tts_service/tts_service_impl.h"
#include "lib/fsl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;

  media::tts::TtsServiceImpl impl(
      app::ApplicationContext::CreateFromStartupInfo());

  if (impl.Init() != ZX_OK)
    return -1;

  loop.Run();
  return 0;
}
