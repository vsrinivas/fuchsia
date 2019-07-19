// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_COMPONENT_MAIN_H_
#define PERIDOT_LIB_TESTING_COMPONENT_MAIN_H_

#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>

namespace modular {
namespace testing {

// A main function for an application that only runs the implementation of a
// single component used for integration testing. The component implementation
// Impl usually derives from ComponentBase.
//
// Args are meant to be either nothing or the instance of a Settings class
// initialized from the command line arguments. They are just passed as
// additional constructor arguments to Impl.
//
// Example use with settings (TestApp and Settings are locally defined classes):
//
//   int main(int argc, const char** argv) {
//     auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
//     Settings settings(command_line);
//     modular::testing::ComponentMain<TestApp, Settings>(std::move(settings));
//     return 0;
//   }
//
// Example use without settings (TestApp is a locally defined class):
//
//   int main(int, const char**) {
//     modular::testing::ComponentMain<TestApp>();
//     return 0;
//   }
//
// The classes ComponentBase and SessionShellBase defined in this directory are
// meant to be used as base classes for Impl.
template <typename Impl, typename... Args>
void ComponentMain(Args... args) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = sys::ComponentContext::Create();
  modular::AppDriver<Impl> driver(context->outgoing(),
                                  std::make_unique<Impl>(context.get(), std::move(args)...),
                                  [&loop] { loop.Quit(); });

  loop.Run();
}

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_COMPONENT_MAIN_H_
