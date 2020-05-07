// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/inspect/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <ctime>

class FizzBuzz final : public fuchsia::examples::inspect::FizzBuzz {
 public:
  FizzBuzz(inspect::Node node) : node_(std::move(node)) {
    incoming_connection_count_ = node_.CreateUint("incoming_connection_count", 0);
    closed_connection_count_ = node_.CreateUint("closed_connection_count", 0);
    request_count_ = node_.CreateUint("request_count", 0);
    request_time_histogram_ = node_.CreateExponentialIntHistogram(
        "request_time_histogram_us", 1 /* floor */, 1 /* initial step */, 2 /* step multiplier */,
        16 /* buckets */);
  }

  // Execute the FizzBuzz algorithm.
  void Execute(uint32_t count, ExecuteCallback callback) {
    request_count_.Add(1);
    std::clock_t start = std::clock();
    std::string output;
    for (uint32_t i = 1; i <= count; i++) {
      if (i != 1) {
        output += " ";
      }
      if (i % 3 == 0) {
        output += "Fizz";
      }
      if (i % 5 == 0) {
        output += "Buzz";
      }
      if (i % 3 != 0 && i % 5 != 0) {
        output += std::to_string(i);
      }
    }

    callback(std::move(output));
    request_time_histogram_.Insert((std::clock() - start) * 1000000 / CLOCKS_PER_SEC);
  }

  fidl::InterfaceRequestHandler<fuchsia::examples::inspect::FizzBuzz> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::examples::inspect::FizzBuzz> request) {
      incoming_connection_count_.Add(1);
      binding_set_.AddBinding(this, std::move(request), nullptr /* dispatcher */,
                              [this](zx_status_t unused) { closed_connection_count_.Add(1); });
    };
  }

 private:
  inspect::Node node_;
  inspect::UintProperty incoming_connection_count_, closed_connection_count_;
  inspect::UintProperty request_count_;
  inspect::ExponentialIntHistogram request_time_histogram_;
  fidl::BindingSet<fuchsia::examples::inspect::FizzBuzz> binding_set_;
};

int main(int argc, char** argv) {
  syslog::SetTags({"inspect_cpp_codelab", "fizzbuzz"});

  FX_LOGS(INFO) << "Starting up...";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  sys::ComponentInspector inspector(context.get());

  FizzBuzz fizzbuzz(inspector.root().CreateChild("fizzbuzz_service"));
  context->outgoing()->AddPublicService(fizzbuzz.GetHandler());

  // Run the loop.
  loop.Run();
  return 0;
}
