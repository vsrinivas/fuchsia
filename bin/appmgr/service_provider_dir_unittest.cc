// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/service_provider_dir_impl.h"

#include <fidl/examples/echo/cpp/fidl.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>

#include "lib/gtest/real_loop_fixture.h"
#include "garnet/bin/appmgr/util.h"

namespace component {
namespace {

class FakeEcho : public fidl::examples::echo::Echo {
 public:
  FakeEcho(fidl::InterfaceRequest<fidl::examples::echo::Echo> request)
      : binding_(this) {
    binding_.Bind(std::move(request));
  };
  ~FakeEcho() override {}
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(answer_);
  }

  void SetAnswer(fidl::StringPtr answer) { answer_ = answer; }

 private:
  fidl::StringPtr answer_;
  ::fidl::Binding<fidl::examples::echo::Echo> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeEcho);
};

class ServiceProviderTest : public gtest::RealLoopFixture {
 public:
  ServiceProviderTest() : vfs_(dispatcher()), value_(0) {}

  fbl::RefPtr<fs::Service> CreateService(int set_value) {
    return fbl::AdoptRef(
        new fs::Service([this, set_value](zx::channel channel) {
          value_ = set_value;
          return ZX_OK;
        }));
  }

  fbl::RefPtr<fs::Service> CreateEchoService(fidl::StringPtr answer) {
    return fbl::AdoptRef(new fs::Service([this, answer](zx::channel channel) {
      auto echo = std::make_unique<FakeEcho>(
          fidl::InterfaceRequest<fidl::examples::echo::Echo>(
              std::move(channel)));
      echo->SetAnswer(answer);
      echo_services_.push_back(std::move(echo));
      return ZX_OK;
    }));
  }

  void GetService(ServiceProviderDirImpl* service_provider,
                  const fbl::String& service_name,
                  fbl::RefPtr<fs::Service>* out) {
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(ZX_OK, service_provider->Lookup(&child, service_name));
    *out = fbl::RefPtr<fs::Service>(static_cast<fs::Service*>(child.get()));
  }

  void TestService(ServiceProviderDirImpl* service_provider,
                   const fbl::String& service_name, int expected_value) {
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(ZX_OK, service_provider->Lookup(&child, service_name));
    fbl::RefPtr<fs::Service> child_node;
    GetService(service_provider, service_name, &child_node);
    if (child_node) {
      child_node->Serve(&vfs_, zx::channel(), 0);
      RunLoopUntilIdle();
      ASSERT_EQ(expected_value, value_);
    }
  }

  zx::channel OpenAsDirectory(fbl::RefPtr<ServiceProviderDirImpl> service) {
    return Util::OpenAsDirectory(&vfs_, service);
  }

 protected:
  fs::SynchronousVfs vfs_;
  int value_;
  std::vector<std::unique_ptr<FakeEcho>> echo_services_;
};

TEST_F(ServiceProviderTest, SimpleService) {
  auto service_name = "fake_service";
  auto service = CreateService(2);
  ServiceProviderDirImpl service_provider;
  service_provider.AddService(service, service_name);
  TestService(&service_provider, service_name, 2);
}

TEST_F(ServiceProviderTest, Parent) {
  ServiceProviderDirImpl service_provider;
  auto parent_service_provider = fbl::AdoptRef(new ServiceProviderDirImpl());
  service_provider.set_parent(parent_service_provider);
  auto service_name1 = "fake_service1";
  auto service_name2 = "fake_service2";
  auto service1 = CreateService(1);
  auto service2 = CreateService(2);
  auto service3 = CreateService(3);

  service_provider.AddService(service1, service_name1);
  parent_service_provider->AddService(service2, service_name2);
  // add same name service to parent
  parent_service_provider->AddService(service3, service_name1);

  // should call child service
  TestService(&service_provider, service_name1, 1);

  // check that we can get parent service
  TestService(&service_provider, service_name2, 2);

  // check that parent is able to access it's service
  TestService(parent_service_provider.get(), service_name1, 3);
}

TEST_F(ServiceProviderTest, BackingDir) {
  ServiceProviderDirImpl service_provider;
  auto parent_service_provider = fbl::AdoptRef(new ServiceProviderDirImpl());
  auto backing_dir = OpenAsDirectory(parent_service_provider);
  service_provider.set_backing_dir(std::move(backing_dir));

  auto service_name1 = "fake_service1";
  auto service_name2 = "fake_service2";
  auto service1 = CreateService(1);
  auto service2 = CreateEchoService("GoodBye");
  auto service3 = CreateService(3);

  service_provider.AddService(service1, service_name1);
  parent_service_provider->AddService(service2, service_name2);
  // add same name service to backing dir
  parent_service_provider->AddService(service3, service_name1);

  // should call child service
  TestService(&service_provider, service_name1, 1);

  // check that parent is able to access it's service
  TestService(parent_service_provider.get(), service_name1, 3);

  // check that we can get backing_dir service from child
  fbl::RefPtr<fs::Service> echo;
  GetService(&service_provider, service_name2, &echo);
  if (echo) {
    fidl::examples::echo::EchoPtr echo_ptr;
    echo->Serve(&vfs_, echo_ptr.NewRequest().TakeChannel(), 0);
    RunLoopUntilIdle();
    fidl::StringPtr message = "bogus";
    echo_ptr->EchoString("Hello World!",
                         [&](::fidl::StringPtr retval) { message = retval; });
    RunLoopUntilIdle();
    EXPECT_EQ("GoodBye", message);
  }
}

TEST_F(ServiceProviderTest, ParentAndBackingDirTogther) {
  ServiceProviderDirImpl service_provider;
  auto parent_service_provider = fbl::AdoptRef(new ServiceProviderDirImpl());
  zx::channel b1, b2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &b1, &b2));
  service_provider.set_backing_dir(std::move(b2));
  service_provider.set_parent(parent_service_provider);

  // test that b2 was invalidated because of setting parent.
  char msg[] = "message";
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, b1.write(0, msg, sizeof(msg), nullptr, 0));

  b1.reset();
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &b1, &b2));
  service_provider.set_backing_dir(std::move(b2));

  // test that we cannot set backing directory after setting parent.
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, b1.write(0, msg, sizeof(msg), nullptr, 0));
}

}  // namespace
}  // namespace component
