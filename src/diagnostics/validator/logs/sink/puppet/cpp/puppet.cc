// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/validate/logs/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

class Puppet : public fuchsia::validate::logs::LogSinkPuppet {
 public:
  explicit Puppet(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {
    context_->outgoing()->AddPublicService(sink_bindings_.GetHandler(this));
  }

  void GetInfo(GetInfoCallback callback) override {
    fuchsia::validate::logs::PuppetInfo info;
    info.pid = GetKoid(zx_process_self());
    info.tid = GetKoid(zx_thread_self());
    callback(info);
  }

  void EmitLog(EmitLogCallback callback) override {
    FX_SLOG(WARNING, "test_log", KV("foo", "bar"));
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
