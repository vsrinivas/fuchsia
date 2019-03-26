// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/namespace.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_request.h"

using fuchsia::sys::ServiceList;
using fuchsia::sys::ServiceListPtr;
using fuchsia::sys::ServiceProvider;
using fuchsia::sys::ServiceProviderPtr;

namespace component {
namespace {

class NamespaceTest : public ::gtest::RealLoopFixture {
 protected:
  fxl::RefPtr<Namespace> MakeNamespace(
      ServiceListPtr additional_services,
      fxl::RefPtr<Namespace> parent = nullptr) {
    return fxl::MakeRefCounted<Namespace>(
        parent, nullptr, std::move(additional_services), nullptr);
  }

  zx_status_t ConnectToService(zx_handle_t svc_dir, const std::string& name) {
    zx::channel h1, h2;
    zx_status_t r = zx::channel::create(0, &h1, &h2);
    if (r != ZX_OK)
      return r;
    fdio_service_connect_at(svc_dir, name.c_str(), h2.release());
    return ZX_OK;
  }
};

class NamespaceHostDirectoryTest : public NamespaceTest {
 protected:
  zx::channel OpenAsDirectory() {
    fidl::InterfaceHandle<fuchsia::io::Directory> dir;
    directory_.Serve(fuchsia::io::OPEN_RIGHT_READABLE,
                     dir.NewRequest().TakeChannel(), dispatcher());
    return dir.TakeChannel();
  }

  zx_status_t AddService(const std::string& name) {
    auto cb = [this, name](zx::channel channel,
                           async_dispatcher_t* dispatcher) {
      ++connection_ctr_[name];
    };
    return directory_.AddEntry(name, std::make_unique<vfs::Service>(cb));
  }

  vfs::PseudoDir directory_;
  std::map<std::string, int> connection_ctr_;
};

class NamespaceProviderTest : public NamespaceTest {
 protected:
  fxl::RefPtr<Namespace> MakeNamespace(ServiceListPtr additional_services) {
    return fxl::MakeRefCounted<Namespace>(
        nullptr, nullptr, std::move(additional_services), nullptr);
  }

  zx_status_t AddService(const std::string& name) {
    fidl::InterfaceRequestHandler<fuchsia::io::Node> cb =
        [this, name](fidl::InterfaceRequest<fuchsia::io::Node> request) {
          ++connection_ctr_[name];
        };
    provider_.AddService(std::move(cb), name);
    return ZX_OK;
  }

  sys::testing::ServiceDirectoryProvider provider_;
  std::map<std::string, int> connection_ctr_;
};

std::pair<std::string, int> StringIntPair(const std::string& s, int i) {
  return {s, i};
}

TEST_F(NamespaceHostDirectoryTest, AdditionalServices) {
  static constexpr char kService1[] = "fuchsia.test.TestService1";
  static constexpr char kService2[] = "fuchsia.test.TestService2";
  ServiceListPtr service_list(new ServiceList);
  service_list->names.push_back(kService1);
  service_list->names.push_back(kService2);
  AddService(kService1);
  AddService(kService2);
  ServiceProviderPtr service_provider;
  service_list->host_directory = OpenAsDirectory();
  auto ns = MakeNamespace(std::move(service_list));

  zx::channel svc_dir = ns->OpenServicesAsDirectory();
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService1));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  // fdio_service_connect_at does not return an error if connection failed.
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), "fuchsia.test.NotExists"));
  RunLoopUntilIdle();
  std::vector<std::pair<std::string, int>> connection_ctr_vec;
  for (const auto& e : connection_ctr_) {
    connection_ctr_vec.push_back(e);
  }
  EXPECT_THAT(connection_ctr_vec,
              ::testing::ElementsAre(StringIntPair(kService1, 1),
                                     StringIntPair(kService2, 2)));
}

TEST_F(NamespaceHostDirectoryTest, AdditionalServices_InheritParent) {
  static constexpr char kService1[] = "fuchsia.test.TestService1";
  static constexpr char kService2[] = "fuchsia.test.TestService2";
  ServiceListPtr parent_service_list(new ServiceList);
  parent_service_list->names.push_back(kService1);
  ServiceListPtr service_list(new ServiceList);
  service_list->names.push_back(kService2);
  AddService(kService1);
  AddService(kService2);
  parent_service_list->host_directory = OpenAsDirectory();
  service_list->host_directory = OpenAsDirectory();
  auto parent_ns = MakeNamespace(std::move(parent_service_list));
  auto ns = MakeNamespace(std::move(service_list), parent_ns);

  zx::channel svc_dir = ns->OpenServicesAsDirectory();
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService1));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  // fdio_service_connect_at does not return an error if connection failed.
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), "fuchsia.test.NotExists"));
  RunLoopUntilIdle();
  std::vector<std::pair<std::string, int>> connection_ctr_vec;
  for (const auto& e : connection_ctr_) {
    connection_ctr_vec.push_back(e);
  }
  EXPECT_THAT(connection_ctr_vec,
              ::testing::ElementsAre(StringIntPair(kService1, 1),
                                     StringIntPair(kService2, 1)));
}

TEST_F(NamespaceProviderTest, AdditionalServices) {
  static constexpr char kService1[] = "fuchsia.test.TestService1";
  static constexpr char kService2[] = "fuchsia.test.TestService2";
  ServiceListPtr service_list(new ServiceList);
  ServiceProviderPtr service_provider;
  service_list->names.push_back(kService1);
  service_list->names.push_back(kService2);
  EXPECT_EQ(ZX_OK, AddService(kService1));
  EXPECT_EQ(ZX_OK, AddService(kService2));

  service_list->host_directory =
      provider_.service_directory()->CloneChannel().TakeChannel();
  auto ns = MakeNamespace(std::move(service_list));

  zx::channel svc_dir = ns->OpenServicesAsDirectory();
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService1));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  // fdio_service_connect_at does not return an error if connection failed.
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), "fuchsia.test.NotExists"));
  RunLoopUntilIdle();
  std::vector<std::pair<std::string, int>> connection_ctr_vec;
  for (const auto& e : connection_ctr_) {
    connection_ctr_vec.push_back(e);
  }
  EXPECT_THAT(connection_ctr_vec,
              ::testing::ElementsAre(StringIntPair(kService1, 1),
                                     StringIntPair(kService2, 2)));
}

}  // namespace
}  // namespace component
