// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crashpad_analyzer_impl.h"

#include <utility>

#include <fuchsia/crash/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/syslog/cpp/logger.h>

int main(int argc, const char** argv) {
  syslog::InitLogger({"crash"});

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> app_context(
      component::StartupContext::CreateFromStartupInfo());

  fuchsia::crash::CrashpadAnalyzerImpl analyzer;
  fidl::BindingSet<::fuchsia::crash::Analyzer> bindings;
  app_context->outgoing().AddPublicService(bindings.GetHandler(&analyzer));

  loop.Run();

  return 0;
}
