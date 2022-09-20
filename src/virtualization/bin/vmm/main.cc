// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>

#include "src/virtualization/bin/vmm/vmm_controller.h"

int main(int argc, char** argv) {
  syslog::SetTags({"vmm"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  vmm::VmmController controller([&loop]() { loop.Shutdown(); },
                                sys::ComponentContext::CreateAndServeOutgoingDirectory(),
                                loop.dispatcher());

  return loop.Run();
}
