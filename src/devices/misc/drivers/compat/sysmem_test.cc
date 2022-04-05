// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.sysmem/cpp/wire_test_base.h>
#include <lib/gtest/test_loop_fixture.h>

#include <fbl/ref_ptr.h>
#include <gtest/gtest.h>

#include "lib/service/llcpp/outgoing_directory.h"
#include "src/devices/lib/compat/symbols.h"
#include "src/devices/lib/driver2/logger.h"
#include "src/devices/misc/drivers/compat/device.h"
#include "src/devices/misc/drivers/compat/driver.h"

namespace {

namespace fio = fuchsia_io;
namespace frunner = fuchsia_component_runner;

class FakeSysmem : public fidl::testing::WireTestBase<fuchsia_sysmem::Allocator>,
                   public fs::Service {
 public:
  explicit FakeSysmem(async_dispatcher_t* dispatcher)
      : fs::Service([dispatcher, this](fidl::ServerEnd<fuchsia_sysmem::Allocator> server) {
          fidl::BindServer(dispatcher, std::move(server), this);
          connection_count_++;
          return ZX_OK;
        }) {}

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Allocator::%s\n", name.data());
  }

  size_t connection_count_ = 0;
};

class SysmemTest : public gtest::TestLoopFixture {
 public:
  SysmemTest() : outgoing_(dispatcher()) {}
  void SetUp() override {
    TestLoopFixture::SetUp();
    fake_sysmem_ = fbl::MakeRefCounted<FakeSysmem>(dispatcher());
    outgoing_.root_dir()->AddEntry(fidl::DiscoverableProtocolName<fuchsia_sysmem::Allocator>,
                                   fake_sysmem_);
    auto ns = CreateNamespaceAndLogger();
    ASSERT_EQ(ZX_OK, ns.status_value());
    ns_ = std::move(ns->first);
    logger_ = std::move(ns->second);
  }

  zx::status<std::pair<driver::Namespace, driver::Logger>> CreateNamespaceAndLogger() {
    auto endpoints = fidl::CreateEndpoints<fio::Directory>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    auto status = outgoing_.Serve(std::move(endpoints->server));
    if (status.is_error()) {
      return status.take_error();
    }

    fidl::Arena arena;
    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> entries(arena, 1);
    entries[0].Allocate(arena);
    entries[0].set_path(arena, "/svc").set_directory(std::move(endpoints->client));

    auto ns = driver::Namespace::Create(entries);
    if (ns.is_error()) {
      return ns.take_error();
    }

    auto logger = driver::Logger::Create(*ns, dispatcher(), "test-logger");
    if (logger.is_error()) {
      return logger.take_error();
    }
    return zx::ok(std::make_pair(std::move(*ns), std::move(*logger)));
  }

 protected:
  fbl::RefPtr<FakeSysmem> fake_sysmem_;
  service::OutgoingDirectory outgoing_;
  driver::Namespace ns_;
  driver::Logger logger_;
  std::optional<compat::Device> device_;
};

TEST_F(SysmemTest, SysmemConnectAllocator) {
  auto [ns, logger] = CreateNamespaceAndLogger().value();
  compat::Driver drv(dispatcher(), {}, std::move(ns), std::move(logger),
                     "fuchsia-boot:///#meta/fake-driver.cm", compat::kDefaultDevice, nullptr);
  compat::Device dev(compat::kDefaultDevice, nullptr, &drv, std::nullopt, logger_, dispatcher());

  zx_device_t* zxdev = dev.ZxDevice();

  ASSERT_EQ(0ul, fake_sysmem_->connection_count_);
  ddk::SysmemProtocolClient client(zxdev, "sysmem");
  ASSERT_TRUE(client.is_valid());

  zx::channel local, remote;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
  client.Connect(std::move(remote));
  ASSERT_TRUE(RunLoopUntilIdle());
  ASSERT_EQ(1ul, fake_sysmem_->connection_count_);
}

}  // namespace
