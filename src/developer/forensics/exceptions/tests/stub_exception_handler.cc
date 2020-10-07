// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/exception/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <zircon/processargs.h>

#include <cstdlib>
#include <memory>

namespace {

// Handle an exception by immediately executing the received callback.
class StubCrashReporter : public fuchsia::exception::internal::CrashReporter {
 public:
  StubCrashReporter(fit::closure on_done) : on_done_(std::move(on_done)) {}

  virtual void Send(zx::exception exception, zx::process process, zx::thread thread,
                    SendCallback callback) override {
    callback();
    on_done_();
  }

 private:
  // Function to execute after Send exectures |callback|.
  ::fit::closure on_done_;
};

}  // namespace

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  zx::channel channel(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  if (!channel.is_valid()) {
    return EXIT_FAILURE;
  }

  // Shutdown the loop immediately after executing |Send|.
  auto stub_crash_reporter = std::make_unique<StubCrashReporter>([&] { loop.Shutdown(); });

  fidl::Binding<StubCrashReporter, std::unique_ptr<fuchsia::exception::internal::CrashReporter>>
      crash_reporter_binding(std::move(stub_crash_reporter), std::move(channel), loop.dispatcher());

  loop.Run();

  return EXIT_SUCCESS;
}
