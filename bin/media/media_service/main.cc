// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "garnet/bin/media/media_service/media_service_impl.h"
#include "lib/fsl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  media::MediaServiceImpl impl(
      app::ApplicationContext::CreateFromStartupInfo());

  loop.Run();
  return 0;
}
