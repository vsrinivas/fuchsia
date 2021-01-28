// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>

#include <string>

#include "codec_factory_app.h"

int main(int argc, char* argv[]) {
  syslog::SetTags({"codec_factory"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "codec_factory trace provider");

  CodecFactoryApp app(loop.dispatcher());

  loop.Run();

  return 0;
}
