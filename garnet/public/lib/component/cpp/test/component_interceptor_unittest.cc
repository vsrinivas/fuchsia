// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fs/service.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/testing/component_interceptor.h>
#include <lib/component/cpp/testing/test_with_environment.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/logging.h>
#include <lib/gtest/real_loop_fixture.h>

#include "gtest/gtest.h"

namespace {

// Records |LoadUrl|s, but forwards requests to a fallback loader.
class TestLoader : fuchsia::sys::Loader {
 public:
  // Fallback loader comes from the supplied |env|.
  TestLoader(const fuchsia::sys::EnvironmentPtr& env) {
    fuchsia::sys::ServiceProviderPtr sp;
    env->GetServices(sp.NewRequest());
    sp->ConnectToService(fuchsia::sys::Loader::Name_,
                         fallback_loader_.NewRequest().TakeChannel());
  }

  virtual ~TestLoader() = default;

  fuchsia::sys::LoaderPtr NewRequest() {
    fuchsia::sys::LoaderPtr loader;
    loader.Bind(bindings_.AddBinding(this));
    return loader;
  }

  // |fuchsia::sys::Loader|
  void LoadUrl(std::string url, LoadUrlCallback response) override {
    requested_urls.push_back(url);
    fallback_loader_->LoadUrl(url, std::move(response));
  }

  std::vector<std::string> requested_urls;

 private:
  fidl::BindingSet<fuchsia::sys::Loader> bindings_;
  fuchsia::sys::LoaderPtr fallback_loader_;
};

}  // namespace

// This fixture gives us a real_env().
class ComponentInterceptorTest
    : public component::testing::TestWithEnvironment {};

// This tests fallback-loader and intercept-url cases using the same enclosing
// environment.
TEST_F(ComponentInterceptorTest, TestFallbackAndInterceptingUrls) {
  TestLoader test_loader(real_env());

  component::testing::ComponentInterceptor interceptor(
      test_loader.NewRequest());
  auto env = component::testing::EnclosingEnvironment::Create(
      "test_harness", real_env(),
      interceptor.MakeEnvironmentServices(real_env()));

  constexpr char kInterceptUrl[] = "file://intercept_url";
  constexpr char kFallbackUrl[] = "file://fallback_url";

  // Test the intercepting case.
  {
    std::string actual_url;

    bool intercepted_url = false;
    interceptor.InterceptURL(
        kInterceptUrl,
        [&actual_url, &intercepted_url](
            fuchsia::sys::StartupInfo startup_info,
            fidl::InterfaceRequest<fuchsia::sys::ComponentController>) {
          intercepted_url = true;
          actual_url = startup_info.launch_info.url;
        });

    fuchsia::sys::ComponentControllerPtr controller;
    fuchsia::sys::LaunchInfo info;
    info.url = kInterceptUrl;
    env->CreateComponent(std::move(info), controller.NewRequest());

    ASSERT_TRUE(RunLoopUntil([&] { return intercepted_url; }));
    EXPECT_EQ(kInterceptUrl, actual_url);
  }

  test_loader.requested_urls.clear();

  // Test the fallback loader case.
  {
    fuchsia::sys::ComponentControllerPtr controller;
    fuchsia::sys::LaunchInfo info;
    info.url = kFallbackUrl;
    // Should this call into our TestLoader.
    env->CreateComponent(std::move(info), controller.NewRequest());

    ASSERT_TRUE(
        RunLoopUntil([&] { return test_loader.requested_urls.size() > 0u; }));

    EXPECT_EQ(kFallbackUrl, test_loader.requested_urls[0]);
  }
}
