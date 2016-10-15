// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/test.h"

#include <magenta/syscalls.h>
#include <mojo/system/main.h>

#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/system/time.h"
#include "mojo/public/cpp/utility/run_loop.h"

using namespace maxwell;
using namespace mojo;

void Yield() {
  // To sleep successfully we need to both yield the thread and process Mojo
  // messages.
  //
  // If we don't yield the thread, other processes run extremely slowly (for
  // example, each dependency may take about 5 seconds to start up). Yielding
  // immediately with 0 is not sufficient to remedy this.
  //
  // If we don't run the message loop, we never receive IPCs.
  mx_nanosleep(500000);  // .5 ms; lower-capped to 1 ms by Magenta
  RunLoop::current()->RunUntilIdle();
}

void StartComponent(mojo::Shell* shell, const std::string& url) {
  InterfacePtr<ServiceProvider> component;
  shell->ConnectToApplication(url, GetProxy(&component));
}

void Sleep(unsigned int millis) {
  MojoTimeTicks deadline = GetTimeTicksNow() + millis * 1000;
  WaitUntil([deadline] { return GetTimeTicksNow() >= deadline; });
}

constexpr MojoTimeTicks kPauseIdle = 250 * 1000;
constexpr MojoTimeTicks kPauseMax = 2000 * 1000;
// This is approximated by updating it when a dependency app is launched.
MojoTimeTicks last_activity;

void Pause() {
  // In practice, the amount of time one needs to pause increases with the
  // number of apps being spun up as a result of a test call. This
  // implementation pauses longer if apps are starting up.
  last_activity = GetTimeTicksNow();
  MojoTimeTicks deadline = last_activity + kPauseMax;
  WaitUntil([deadline] {
    MojoTimeTicks now = GetTimeTicksNow();

    return now >= last_activity + kPauseIdle || now >= deadline;
  });
}

#define TEST(name) void name(Shell*)

// TODO(rosswang): Can we register them at the same time?
TEST(TestSuggestionEngine);

namespace {

class MaxwellTestApp : public ApplicationImplBase, public TestParent {
 public:
  void OnInitialize() override {
    srand(time(NULL));

    // We have to wrap the test runs in a delayed task because until
    // OnInitialize returns, OnAcceptConnection will not be called, even if we
    // yield the thread and process messages.
    RunLoop::current()->PostDelayedTask(
        [this] {
          Test tests[]{{"TestSuggestionEngine", &TestSuggestionEngine}};

          for (const auto& test : tests) {
            MOJO_LOG(INFO) << test.name << ": ";

            test.run(shell());
            MOJO_LOG(INFO) << "Success!";
            Pause();  // Wait for any lagging processes to start up.
            child_apps_.ForAllPtrs([](Debug* debug) { debug->Kill(); });
            Pause();  // Wait for killed processes to shut down.
          }

          MOJO_LOG(INFO) << "All tests completed.";
        },
        0);
  }

  void RegisterChildDependency(const mojo::String& url) override {
    MOJO_LOG(INFO) << "RegisterChildDependency(" << url << ")";
    DebugPtr debug;
    ConnectToService(shell(), url, GetProxy(&debug));
    if (debug)
      child_apps_.AddInterfacePtr(std::move(debug));

    last_activity = GetTimeTicksNow();
  }

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<TestParent>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<TestParent> request) {
          test_parent_bindings_.AddBinding(this, std::move(request));

          // Also register the requester as a new dependency.
          RegisterChildDependency(connection_context.remote_url);
        });
    return true;
  }

 private:
  BindingSet<TestParent> test_parent_bindings_;
  InterfacePtrSet<Debug> child_apps_;
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  MaxwellTestApp test;
  return mojo::RunApplication(request, &test);
}
