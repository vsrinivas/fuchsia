// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <trace/observer.h>

#include "peridot/tests/benchmark/story/tracing_waiter.h"

namespace {

// This Module updates its root link 100 times and then just sits there until
// it's terminated.
class NullModule : fuchsia::modular::LinkWatcher {
 public:
  NullModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host), link_watcher_binding_(this) {
    FXL_LOG(INFO) << "NullModule()";
    module_host_->module_context()->GetLink(nullptr, link_.NewRequest());

    // Will call Notify() with current value.
    link_->WatchAll(link_watcher_binding_.NewBinding());
  }

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) { done(); }

 private:
  // |fuchsia::modular::LinkWatcher|
  void Notify(fidl::StringPtr json) override {
    FXL_LOG(INFO) << "Notify() " << json;

    // First invocation is from WatchAll(); next from Set().
    if (count_ == -1) {
      count_ = 0;
      tracing_waiter_.WaitForTracing([this] { Set(); });
      return;
    }

    // Corresponding TRACE_ASYNC_BEGIN() is in Set().
    TRACE_ASYNC_END("benchmark", "link/set", count_);

    ++count_;
    if (count_ <= 100) {
      Set();
    }
  }

  void Set() {
    FXL_LOG(INFO) << "Set() " << count_;

    // Corresponding TRACE_ASYNC_END() is in Notify().
    TRACE_ASYNC_BEGIN("benchmark", "link/set", count_);

    // Corresponding TRACE_FLOW_END() is in the user shell.
    TRACE_FLOW_BEGIN("benchmark", "link/trans", count_);

    link_->Set(nullptr, std::to_string(count_));
  }

  modular::ModuleHost* const module_host_;
  modular::TracingWaiter tracing_waiter_;
  fuchsia::modular::LinkPtr link_;
  fidl::Binding<fuchsia::modular::LinkWatcher> link_watcher_binding_;

  int count_{-1};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<NullModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
