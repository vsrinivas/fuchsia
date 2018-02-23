// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "garnet/bin/media/media_service/media_service_impl.h"
#include "lib/fsl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  bool transient = false;
  for (int arg_index = 0; arg_index < argc; ++arg_index) {
    if (argv[arg_index] == media::MediaServiceImpl::kIsolateArgument) {
      transient = true;
      break;
    }
  }

  fsl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  media::MediaServiceImpl impl(app::ApplicationContext::CreateFromStartupInfo(),
                               transient);

  loop.Run();
  return 0;
}
