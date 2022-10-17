// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.sysmem/cpp/wire_test_base.h>
#include <lib/async-testing/test_loop.h>
#include <lib/driver2/logger.h>
#include <lib/driver_compat/symbols.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <fbl/ref_ptr.h>
#include <gtest/gtest.h>

#include "sdk/lib/driver_runtime/testing/loop_fixture/test_loop_fixture.h"
#include "src/devices/misc/drivers/compat/device.h"
#include "src/devices/misc/drivers/compat/driver.h"

namespace {

namespace fio = fuchsia_io;
namespace frunner = fuchsia_component_runner;

class FakeSysmem : public fidl::testing::WireTestBase<fuchsia_sysmem::Allocator>,
                   public fbl::RefCounted<FakeSysmem> {
 public:
  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Allocator::%s\n", name.data());
  }

  size_t connection_count_ = 0;
};

class SysmemTest : public gtest::DriverTestLoopFixture {
 public:
  void SetUp() override {
    DriverTestLoopFixture::SetUp();
    fake_sysmem_ = fbl::MakeRefCounted<FakeSysmem>();
  }

  zx::result<std::vector<frunner::ComponentNamespaceEntry>> CreateNamespace() {
    ns_server_ = component::OutgoingDirectory::Create(dispatcher());
    auto add_protocol_result = ns_server_->AddProtocol<fuchsia_sysmem::Allocator>(
        [this](fidl::ServerEnd<fuchsia_sysmem::Allocator> server) {
          fidl::BindServer(dispatcher(), std::move(server), fake_sysmem_.get());
          fake_sysmem_->connection_count_++;
        });
    ZX_ASSERT(add_protocol_result.is_ok());

    auto endpoints = fidl::CreateEndpoints<fio::Directory>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    auto status = ns_server_->Serve(std::move(endpoints->server));
    if (status.is_error()) {
      return status.take_error();
    }

    auto entry = frunner::ComponentNamespaceEntry(
        {.path = std::string("/"), .directory = std::move(endpoints->client)});
    std::vector<frunner::ComponentNamespaceEntry> entries;
    entries.push_back(std::move(entry));

    return zx::ok(std::move(entries));
  }

 protected:
  bool RunTestLoopUntilIdle() { return test_loop_.RunUntilIdle(); }

  async_dispatcher_t* dispatcher() { return test_loop_.dispatcher(); }

  std::optional<component::OutgoingDirectory> ns_server_;
  fbl::RefPtr<FakeSysmem> fake_sysmem_;
  async::TestLoop test_loop_;
};

TEST_F(SysmemTest, SysmemConnectAllocator) {
  auto outgoing = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_EQ(ZX_OK, outgoing.status_value());
  auto node_endpoints = fidl::CreateEndpoints<fuchsia_driver_framework::Node>();
  ASSERT_EQ(ZX_OK, node_endpoints.status_value());
  auto unowned_driver_dispatcher = fdf::UnownedDispatcher(driver_dispatcher());
  auto ns = CreateNamespace();
  ASSERT_EQ(ZX_OK, ns.status_value());
  driver::DriverStartArgs start_args({.node = std::move(node_endpoints->client),
                                      .symbols = std::nullopt,
                                      .url = std::string("fuchsia-boot:///#meta/fake-driver.cm"),
                                      .program = std::nullopt,
                                      .ns = std::move(ns.value()),
                                      .outgoing_dir = std::move(outgoing->server),
                                      .config = std::nullopt});
  compat::Driver drv(std::move(start_args), std::move(unowned_driver_dispatcher),
                     compat::kDefaultDevice, nullptr, "/pkg/compat");
  compat::Device dev(compat::kDefaultDevice, nullptr, &drv, std::nullopt, drv.logger(),
                     driver_dispatcher().async_dispatcher());

  zx_device_t* zxdev = dev.ZxDevice();
  ASSERT_EQ(0ul, fake_sysmem_->connection_count_);
  ddk::SysmemProtocolClient client(zxdev, "sysmem");
  ASSERT_TRUE(client.is_valid());
  zx::channel local, remote;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
  client.Connect(std::move(remote));
  RunTestLoopUntilIdle();
  ASSERT_EQ(1ul, fake_sysmem_->connection_count_);
  ShutdownDriverDispatcher();
}

}  // namespace
