// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/package_updating_loader.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fidl/examples/echo/cpp/fidl.h>
#include <fs/service.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/pkg/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include "gtest/gtest.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace sysmgr {
namespace {

class PackageResolverMock : public fuchsia::pkg::PackageResolver {
 public:
  explicit PackageResolverMock(zx_status_t status)
      : status_(status) {}

  virtual void Resolve(::fidl::StringPtr package_uri,
                       ::fidl::VectorPtr<::fidl::StringPtr> selectors,
                       fuchsia::pkg::UpdatePolicy update_policy,
                       ::fidl::InterfaceRequest<fuchsia::io::Directory> dir,
                       ResolveCallback callback) override {
    std::vector<std::string> v_selectors;
    for (const auto& s : selectors.get()) {
      v_selectors.push_back(s.get());
    }
    args_ = std::make_tuple(package_uri.get(), v_selectors, update_policy);
    dir_channels_.push_back(dir.TakeChannel());
    callback(status_);
  }

  void AddBinding(fidl::InterfaceRequest<fuchsia::pkg::PackageResolver> req) {
    bindings_.AddBinding(this, std::move(req));
  }

  typedef std::tuple<std::string, std::vector<std::string>,
                     fuchsia::pkg::UpdatePolicy>
      ArgsTuple;
  const ArgsTuple& args() const { return args_; }

 private:
  const zx_status_t status_;
  ArgsTuple args_;
  std::vector<zx::channel> dir_channels_;
  fidl::BindingSet<fuchsia::pkg::PackageResolver> bindings_;
};

constexpr char kRealm[] = "package_updating_loader_env";

class PackageUpdatingLoaderTest
    : public component::testing::TestWithEnvironment {
 protected:
  void Init(PackageResolverMock* resolver_service) {
    fuchsia::pkg::PackageResolverPtr resolver;
    resolver_service->AddBinding(resolver.NewRequest(dispatcher()));
    loader_ = std::make_unique<PackageUpdatingLoader>(
        std::unordered_set<std::string>{"my_resolver"}, std::move(resolver),
        dispatcher());
    loader_service_ =
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          loader_->AddBinding(
              fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
          return ZX_OK;
        }));
    auto services = CreateServicesWithCustomLoader(loader_service_);
    env_ = CreateNewEnclosingEnvironment(kRealm, std::move(services));
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url,
                                            zx::channel dir) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.directory_request = std::move(dir);
    return launch_info;
  }

  template <typename RequestType>
  void ConnectToServiceAt(zx::channel dir,
                          fidl::InterfaceRequest<RequestType> req) {
    ASSERT_EQ(ZX_OK, fdio_service_connect_at(dir.release(), RequestType::Name_,
                                             req.TakeChannel().release()));
  }

  std::unique_ptr<component::testing::EnclosingEnvironment> env_;

 private:
  std::unique_ptr<PackageUpdatingLoader> loader_;
  fbl::RefPtr<fs::Service> loader_service_;
};

TEST_F(PackageUpdatingLoaderTest, Success) {
  PackageResolverMock resolver_service(ZX_OK);
  Init(&resolver_service);

  // Launch a component in the environment, and prove it started successfully
  // by trying to use a service offered by it.
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  auto launch_info = CreateLaunchInfo(
      "fuchsia-pkg://fuchsia.com/echo2_server_cpp#meta/echo2_server_cpp.cmx",
      std::move(h2));
  auto controller = env_->CreateComponent(std::move(launch_info));
  fidl::examples::echo::EchoPtr echo;
  ConnectToServiceAt(std::move(h1), echo.NewRequest());
  const std::string message = "component launched";
  fidl::StringPtr ret_msg = "";
  echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return std::string(ret_msg) == message; }, zx::sec(10)));

  // Verify that Resolve was called with the expected arguments.
  fuchsia::pkg::UpdatePolicy policy;
  policy.fetch_if_absent = true;
  constexpr char kResolvedUrl[] =
      "fuchsia-pkg://fuchsia.com/echo2_server_cpp/0";
  EXPECT_EQ(
      resolver_service.args(),
      std::make_tuple(std::string(kResolvedUrl),
                      std::vector<std::string>{}, std::move(policy)));
}

TEST_F(PackageUpdatingLoaderTest, Failure) {
  PackageResolverMock resolver_service(ZX_ERR_NOT_FOUND);
  Init(&resolver_service);

  // Launch a component in the environment, and prove it started successfully
  // by trying to use a service offered by it. Note: launching the component
  // should succeed even though the update failed.
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  auto launch_info = CreateLaunchInfo(
      "fuchsia-pkg://fuchsia.com/echo2_server_cpp#meta/echo2_server_cpp.cmx",
      std::move(h2));
  auto controller = env_->CreateComponent(std::move(launch_info));
  fidl::examples::echo::EchoPtr echo;
  ConnectToServiceAt(std::move(h1), echo.NewRequest());
  const std::string message = "component launched";
  fidl::StringPtr ret_msg = "";
  echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval; });
  // Even though the update failed, the loader should load the component anyway.
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return std::string(ret_msg) == message; }, zx::sec(10)));
}

}  // namespace
}  // namespace sysmgr
