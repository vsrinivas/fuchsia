// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/termination_reason.h>
#include <stdio.h>
#include <unistd.h>

#include <fuchsia/testing/appmgr/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <zircon/syscalls.h>
#include "lib/component/cpp/environment_services.h"
#include "lib/component/cpp/testing/enclosing_environment.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/fidl/cpp/binding_set.h"

constexpr char kRealm[] = "namespace_test_realm";

using fuchsia::testing::appmgr::TestService;
using fuchsia::testing::appmgr::TestService2;

class TestServiceImpl : public TestService {
 public:
  void GetMessage(GetMessageCallback callback) override {
    callback("hello");
  }

  fidl::InterfaceRequestHandler<TestService> GetHandler() {
    return bindings_.GetHandler(this);
  }

 private:
  fidl::BindingSet<TestService> bindings_;
};

class TestService2Impl : public TestService2 {
 public:
  void GetMessage(GetMessageCallback callback) override {
    callback("hello2");
  }

  fidl::InterfaceRequestHandler<TestService2> GetHandler() {
    return bindings_.GetHandler(this);
  }

 private:
  fidl::BindingSet<TestService2> bindings_;
};

int main(int argc, const char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <package_url>\n", argv[0]);
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fuchsia::sys::EnvironmentPtr parent_env;
  component::ConnectToEnvironmentService(parent_env.NewRequest());

  TestServiceImpl test_service;
  TestService2Impl test_service2;
  auto enclosing_env = component::testing::EnclosingEnvironment::Create(
      kRealm, parent_env);
  enclosing_env->AddService(test_service.GetHandler());
  enclosing_env->AddService(test_service2.GetHandler());

  const std::string program_url = argv[1];
  auto controller = enclosing_env->CreateComponentFromUrl(program_url);

  controller.events().OnTerminated = [&program_url](
      int64_t return_code,
      fuchsia::sys::TerminationReason termination_reason) {
    if (termination_reason != fuchsia::sys::TerminationReason::EXITED) {
      fprintf(stderr, "%s: %s\n", program_url.c_str(),
              component::HumanReadableTerminationReason(termination_reason)
                  .c_str());
    }
    zx_process_exit(return_code);
  };

  loop.Run();
  return 0;
}