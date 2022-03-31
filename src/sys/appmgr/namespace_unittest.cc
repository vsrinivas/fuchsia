// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/namespace.h"

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

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_request.h"

using fuchsia::sys::ServiceList;
using fuchsia::sys::ServiceListPtr;
using fuchsia::sys::ServiceProvider;
using fuchsia::sys::ServiceProviderPtr;

namespace component {
namespace {

class NamespaceGuard {
 public:
  explicit NamespaceGuard(fxl::RefPtr<Namespace> ns) : ns_(std::move(ns)) {}
  NamespaceGuard(std::nullptr_t) : ns_(nullptr) {}
  ~NamespaceGuard() { Kill(); }
  Namespace* operator->() const { return ns_.get(); }

  fxl::RefPtr<Namespace>& ns() { return ns_; }

  void Kill() {
    if (ns_) {
      ns_->FlushAndShutdown(ns_);
    }
    ns_ = nullptr;
  }

 private:
  fxl::RefPtr<Namespace> ns_;
};

class NamespaceTest : public ::gtest::RealLoopFixture {
 protected:
  NamespaceGuard MakeNamespace(ServiceListPtr additional_services,
                               NamespaceGuard parent = nullptr) {
    if (parent.ns().get() == nullptr) {
      return NamespaceGuard(
          fxl::MakeRefCounted<Namespace>(nullptr, std::move(additional_services), nullptr));
    }
    return NamespaceGuard(Namespace::CreateChildNamespace(parent.ns(), nullptr,
                                                          std::move(additional_services), nullptr));
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

TEST_F(NamespaceTest, NoReferencesToParentAfterItIsShutDown) {
  ServiceListPtr service_list(new ServiceList);
  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  service_list->host_directory = dir.TakeChannel();
  auto parent_ns = MakeNamespace(std::move(service_list));
  auto child_ns = Namespace::CreateChildNamespace(parent_ns.ns(), nullptr, nullptr, nullptr);
  // When creating a child namespace, parent also stores a reference to namespace. As we already
  // have a reference here, make sure that we have more than one reference of this namespace. After
  // `FlushAndShutdown` is called reference stored in parent should go away.
  EXPECT_FALSE(child_ns->HasOneRef());
  bool ns_killed = false;
  child_ns->FlushAndShutdown(child_ns, [&]() {
    // After child_ns is shut down, no one else is holding references to it anymore.
    EXPECT_TRUE(child_ns->HasOneRef());
    ns_killed = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(ns_killed);
}

TEST_F(NamespaceTest, KillNamespaceWithNoParent) {
  auto ns = fxl::MakeRefCounted<Namespace>(nullptr, nullptr, nullptr);
  // no one else should be holding a reference
  EXPECT_TRUE(ns->HasOneRef());
  bool ns_killed = false;
  ns->FlushAndShutdown(ns, [&]() {
    // make sure we have one ns reference so that it can be killed after shutdown.
    EXPECT_TRUE(ns->HasOneRef());
    ns_killed = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(ns_killed);
}

class NamespaceHostDirectoryTest : public NamespaceTest {
 protected:
  zx::channel OpenAsDirectory() {
    fidl::InterfaceHandle<fuchsia::io::Directory> dir;
    directory_.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                     dir.NewRequest().TakeChannel(), dispatcher());
    return dir.TakeChannel();
  }

  zx_status_t AddService(const std::string& name) {
    auto cb = [this, name](zx::channel channel, async_dispatcher_t* dispatcher) {
      ++connection_ctr_[name];
    };
    return directory_.AddEntry(name, std::make_unique<vfs::Service>(cb));
  }

  vfs::PseudoDir directory_;
  std::map<std::string, int> connection_ctr_;
};

class NamespaceProviderTest : public NamespaceTest {
 protected:
  NamespaceGuard MakeNamespace(ServiceListPtr additional_services) {
    return NamespaceGuard(
        fxl::MakeRefCounted<Namespace>(nullptr, std::move(additional_services), nullptr));
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

std::pair<std::string, int> StringIntPair(const std::string& s, int i) { return {s, i}; }

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
              ::testing::ElementsAre(StringIntPair(kService1, 1), StringIntPair(kService2, 2)));
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
              ::testing::ElementsAre(StringIntPair(kService1, 1), StringIntPair(kService2, 1)));
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

  service_list->host_directory = provider_.service_directory()->CloneChannel().TakeChannel();
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
              ::testing::ElementsAre(StringIntPair(kService1, 1), StringIntPair(kService2, 2)));
}

// test that service is connected even when namespace dies right after connect request.
TEST_F(NamespaceHostDirectoryTest, AdditionalServices_NsDies) {
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
  ns.Kill();
  RunLoopUntilIdle();
  std::vector<std::pair<std::string, int>> connection_ctr_vec;
  for (const auto& e : connection_ctr_) {
    connection_ctr_vec.push_back(e);
  }
  EXPECT_THAT(connection_ctr_vec,
              ::testing::ElementsAre(StringIntPair(kService1, 1), StringIntPair(kService2, 2)));
  connection_ctr_.clear();
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService1));  // cannot make anymore connections
  RunLoopUntilIdle();
  // we should not see anymore processed conenction requests.
  EXPECT_EQ(0u, connection_ctr_.size());
}

// test that service in parent is connected even when namespace dies right after connect request.
TEST_F(NamespaceHostDirectoryTest, AdditionalServices_InheritParent_nsDies) {
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
  ns.Kill();
  RunLoopUntilIdle();
  std::vector<std::pair<std::string, int>> connection_ctr_vec;
  for (const auto& e : connection_ctr_) {
    connection_ctr_vec.push_back(e);
  }
  EXPECT_THAT(connection_ctr_vec,
              ::testing::ElementsAre(StringIntPair(kService1, 1), StringIntPair(kService2, 1)));
}

}  // namespace
}  // namespace component
