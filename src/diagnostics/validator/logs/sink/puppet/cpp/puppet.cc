// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/validate/logs/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

class Puppet : public fuchsia::validate::logs::LogSinkPuppet {
 public:
  explicit Puppet(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {
    context_->outgoing()->AddPublicService(sink_bindings_.GetHandler(this));
  }

  void EmitLog(EmitLogCallback callback) override {
    FX_SLOG(WARNING)("test_log", {syslog::LogKey("foo") = "bar"});
    callback();
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::validate::logs::LogSinkPuppet> sink_bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  Puppet puppet(sys::ComponentContext::CreateAndServeOutgoingDirectory());
  FX_LOGS(INFO) << "Puppet started.";
  loop.Run();
}
