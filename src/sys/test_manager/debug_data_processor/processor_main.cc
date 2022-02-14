// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/test/debug/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>

#include <list>

#include "abstract_data_processor.h"
#include "data_processor.h"
#include "data_processor_fidl.h"

namespace ftest_debug = fuchsia::test::debug;

int main(int argc, const char** argv) {
  syslog::SetTags({"debug_data_processor"});
  FX_LOGS(INFO) << "Started debug data processor";
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto dispatcher = loop.dispatcher();
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  async::Loop data_processor_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  data_processor_loop.StartThread();
  auto data_processor_dispatcher = data_processor_loop.dispatcher();

  std::list<std::unique_ptr<DataProcessorFidl>> processors;
  fidl::InterfaceRequestHandler<ftest_debug::DebugDataProcessor> handler =
      [&](fidl::InterfaceRequest<ftest_debug::DebugDataProcessor> request) {
        FX_LOGS(INFO) << "Handling a debug data connection";
        auto it = processors.emplace(processors.begin());
        std::unique_ptr<DataProcessorFidl> actual = std::make_unique<DataProcessorFidl>(
            std::move(request), [&processors, it = it]() { processors.erase(it); },
            [&](fbl::unique_fd fd) {
              // We run data processing on another thread, as it requires many blocking writes
              // to the filesystem.
              return std::make_unique<DataProcessor>(std::move(fd), data_processor_dispatcher);
            },
            dispatcher);
        it->swap(actual);
      };
  context->outgoing()->AddPublicService(std::move(handler));
  loop.Run();
  data_processor_loop.Quit();
}
