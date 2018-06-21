// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>
#include <test/sysmgr/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"

namespace sysmgr {
namespace test {
namespace {

class Service : public ::test::sysmgr::Interface {
 public:
  Service() : context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()) {
    context_->outgoing().AddPublicService(bindings_.GetHandler(this));
  }

  ~Service() = default;

  void Ping(PingCallback callback) override {
    callback("test_sysmgr_service_startup");
  }

 private:
  std::unique_ptr<fuchsia::sys::StartupContext> context_;
  fidl::BindingSet<::test::sysmgr::Interface> bindings_;
};

}  // namespace
}  // namespace test
}  // namespace sysmgr

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  sysmgr::test::Service service;
  loop.Run();
  return 0;
}
