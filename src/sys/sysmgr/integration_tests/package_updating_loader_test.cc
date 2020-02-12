// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/sysmgr/package_updating_loader.h"

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/pkg/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fidl/examples/echo/cpp/fidl.h>

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"

namespace sysmgr {
namespace {

const char kEchoServerURL[] =
    "fuchsia-pkg://fuchsia.com/sysmgr_integration_tests#meta/"
    "echo_server_rust.cmx";

class PackageResolverMock : public fuchsia::pkg::PackageResolver {
 public:
  explicit PackageResolverMock(zx_status_t status) : status_(status) {}

  virtual void Resolve(::std::string package_uri, ::std::vector<::std::string> selectors,
                       fuchsia::pkg::UpdatePolicy update_policy,
                       ::fidl::InterfaceRequest<fuchsia::io::Directory> dir,
                       ResolveCallback callback) override {
    std::vector<std::string> v_selectors;
    for (const auto& s : selectors) {
      v_selectors.push_back(s);
    }
    args_ = std::make_tuple(package_uri, v_selectors, update_policy);
    fdio_service_connect("/pkg", dir.TakeChannel().release());
    callback(status_);
  }

  virtual void GetHash(fuchsia::pkg::PackageUrl package_url, GetHashCallback callback) override {
    callback(fuchsia::pkg::PackageResolver_GetHash_Result::WithErr(ZX_ERR_UNAVAILABLE));
  }

  void AddBinding(fidl::InterfaceRequest<fuchsia::pkg::PackageResolver> req) {
    bindings_.AddBinding(this, std::move(req));
  }

  void Unbind() { bindings_.CloseAll(); }

  typedef std::tuple<std::string, std::vector<std::string>, fuchsia::pkg::UpdatePolicy> ArgsTuple;
  const ArgsTuple& args() const { return args_; }

 private:
  const zx_status_t status_;
  ArgsTuple args_;
  fidl::BindingSet<fuchsia::pkg::PackageResolver> bindings_;
};

class ServiceProviderMock : fuchsia::sys::ServiceProvider {
 public:
  explicit ServiceProviderMock(PackageResolverMock* resolver_service)
      : num_connections_made_(0), resolver_service_(resolver_service) {}

  void ConnectToService(::std::string service_name, ::zx::channel channel) override {
    if (service_name != fuchsia::pkg::PackageResolver::Name_) {
      FXL_LOG(FATAL) << "ServiceProviderMock asked to connect to '" << service_name
                     << "' but we can only connect to the package resolver.";
      return;
    }

    FXL_DLOG(INFO) << "Adding a binding for the package resolver";
    resolver_service_->AddBinding(
        fidl::InterfaceRequest<fuchsia::pkg::PackageResolver>(std::move(channel)));
    num_connections_made_++;
  }

  void DisconnectAll() {
    FXL_DLOG(INFO) << "Disconnecting package resolver mock clients.";
    resolver_service_->Unbind();
  }

  fuchsia::sys::ServiceProviderPtr Bind() {
    fuchsia::sys::ServiceProviderPtr env_services;
    bindings_.AddBinding(this, env_services.NewRequest());
    return env_services;
  }

  int num_connections_made_;

 private:
  PackageResolverMock* resolver_service_;
  fidl::BindingSet<fuchsia::sys::ServiceProvider> bindings_;
};

constexpr char kRealm[] = "package_updating_loader_env";

class PackageUpdatingLoaderTest : public sys::testing::TestWithEnvironment {
 protected:
  void Init(ServiceProviderMock* provider_service) {
    loader_ = std::make_unique<PackageUpdatingLoader>(
        std::unordered_set<std::string>{"my_resolver"}, provider_service->Bind(), dispatcher());
    loader_service_ =
        std::make_shared<vfs::Service>([this](zx::channel channel, async_dispatcher_t* dispatcher) {
          loader_->AddBinding(fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
        });
    sys::testing::EnvironmentServices::ParentOverrides parent_overides;
    parent_overides.loader_service_ = loader_service_;
    auto services = CreateServicesWithParentOverrides(std::move(parent_overides));
    env_ = CreateNewEnclosingEnvironment(kRealm, std::move(services));
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url, zx::channel dir) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.directory_request = std::move(dir);
    return launch_info;
  }

  template <typename RequestType>
  void ConnectToServiceAt(zx::channel dir, fidl::InterfaceRequest<RequestType> req) {
    ASSERT_EQ(ZX_OK, fdio_service_connect_at(dir.release(), RequestType::Name_,
                                             req.TakeChannel().release()));
  }

  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;

 private:
  std::unique_ptr<PackageUpdatingLoader> loader_;
  std::shared_ptr<vfs::Service> loader_service_;
};

TEST_F(PackageUpdatingLoaderTest, Success) {
  PackageResolverMock resolver_service(ZX_OK);
  ServiceProviderMock provider_service(&resolver_service);
  Init(&provider_service);

  // Launch a component in the environment, and prove it started successfully
  // by trying to use a service offered by it.
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  auto launch_info = CreateLaunchInfo(kEchoServerURL, std::move(h2));
  auto controller = env_->CreateComponent(std::move(launch_info));
  fidl::examples::echo::EchoPtr echo;
  ConnectToServiceAt(std::move(h1), echo.NewRequest());
  const std::string message = "component launched";
  std::string ret_msg = "";
  echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval.value_or(""); });
  RunLoopUntil([&] { return ret_msg == message; });

  // Verify that Resolve was called with the expected arguments.
  fuchsia::pkg::UpdatePolicy policy;
  policy.fetch_if_absent = true;
  constexpr char kResolvedUrl[] = "fuchsia-pkg://fuchsia.com/sysmgr_integration_tests/0";
  const auto& args = resolver_service.args();
  EXPECT_EQ(std::get<0>(args), std::string(kResolvedUrl));
  EXPECT_EQ(std::get<1>(args), std::vector<std::string>{});
  EXPECT_TRUE(fidl::Equals(std::get<2>(args), policy));
}

TEST_F(PackageUpdatingLoaderTest, Failure) {
  PackageResolverMock resolver_service(ZX_ERR_NOT_FOUND);
  ServiceProviderMock provider_service(&resolver_service);
  Init(&provider_service);

  // Launch a component in the environment, and prove it started successfully
  // by trying to use a service offered by it. Note: launching the component
  // should succeed even though the update failed.
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  auto launch_info = CreateLaunchInfo(kEchoServerURL, std::move(h2));
  auto controller = env_->CreateComponent(std::move(launch_info));
  fidl::examples::echo::EchoPtr echo;
  ConnectToServiceAt(std::move(h1), echo.NewRequest());
  const std::string message = "component launched";
  std::string ret_msg = "";
  echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval.value_or(""); });
  // Even though the update failed, the loader should load the component anyway.
  RunLoopUntil([&] { return ret_msg == message; });
}

TEST_F(PackageUpdatingLoaderTest, HandleResolverDisconnectCorrectly) {
  PackageResolverMock resolver_service(ZX_OK);
  ServiceProviderMock service_provider(&resolver_service);
  Init(&service_provider);

  auto launch_url = kEchoServerURL;
  {
    // Launch a component in the environment, and prove it started successfully
    // by trying to use a service offered by it.
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    auto launch_info = CreateLaunchInfo(launch_url, std::move(h2));
    auto controller = env_->CreateComponent(std::move(launch_info));

    fidl::examples::echo::EchoPtr echo;
    ConnectToServiceAt(std::move(h1), echo.NewRequest());

    const std::string message = "component launched";
    std::string ret_msg = "";

    echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval.value_or(""); });
    RunLoopUntil([&] { return ret_msg == message; });
  }

  // since the connection to the package resolver is initiated lazily, we need
  // to make sure that after a first successful connection we can still recover
  // by reconnecting
  service_provider.DisconnectAll();

  {
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    FXL_LOG(INFO) << "serviceprovider disconnected, new echo channels created";
    auto launch_info = CreateLaunchInfo(launch_url, std::move(h2));
    auto controller = env_->CreateComponent(std::move(launch_info));

    FXL_LOG(INFO) << "connecting to echo service the second.";
    fidl::examples::echo::EchoPtr echo;
    ConnectToServiceAt(std::move(h1), echo.NewRequest());

    const std::string message = "component launched";
    std::string ret_msg = "";

    FXL_LOG(INFO) << "sending echo message.";
    echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval.value_or(""); });
    RunLoopUntil([&] { return ret_msg == message; });
  }

  // an initial connection and a retry
  ASSERT_EQ(service_provider.num_connections_made_, 2);

  // we'll go through one more time to make sure we're behaving as expected
  service_provider.DisconnectAll();

  {
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    FXL_LOG(INFO) << "serviceprovider disconnected, new echo channels created";
    auto launch_info = CreateLaunchInfo(launch_url, std::move(h2));
    auto controller = env_->CreateComponent(std::move(launch_info));

    FXL_LOG(INFO) << "connecting to echo service the second.";
    fidl::examples::echo::EchoPtr echo;
    ConnectToServiceAt(std::move(h1), echo.NewRequest());

    const std::string message = "component launched";
    std::string ret_msg = "";

    FXL_LOG(INFO) << "sending echo message.";
    echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval.value_or(""); });
    RunLoopUntil([&] { return ret_msg == message; });
  }

  // one more connection
  ASSERT_EQ(service_provider.num_connections_made_, 3);
}

}  // namespace
}  // namespace sysmgr
