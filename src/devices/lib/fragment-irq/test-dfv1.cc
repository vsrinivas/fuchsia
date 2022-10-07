// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.interrupt/cpp/fidl.h>
#include <lib/sys/component/cpp/handlers.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <gtest/gtest.h>

#include "src/devices/lib/fragment-irq/dfv1/fragment-irq.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace fint = fuchsia_hardware_interrupt;

class Dfv1Test : public gtest::TestLoopFixture, public fidl::Server<fint::Provider> {
 public:
  void SetUp() override {
    root_ = MockDevice::FakeRootParent();
    component::ServiceInstanceHandler handler;
    fint::Service::Handler service(&handler);

    auto provider_handler = [this](fidl::ServerEnd<fint::Provider> request) {
      fidl::BindServer(dispatcher(), std::move(request), this);
    };

    auto result = service.add_provider(std::move(provider_handler));
    ASSERT_TRUE(result.is_ok());

    result = outgoing_.AddService<fuchsia_hardware_interrupt::Service>(std::move(handler));
    ASSERT_TRUE(result.is_ok());

    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_EQ(ZX_OK, endpoints.status_value());

    ASSERT_EQ(ZX_OK, outgoing_.Serve(std::move(endpoints->server)).status_value());

    root_->AddFidlService(fint::Service::Name, std::move(endpoints->client), "irq001");
  }

  void Get(GetCompleter::Sync& completer) override {
    fint::ProviderGetResponse ret;
    zx::interrupt fake;
    ASSERT_EQ(ZX_OK, zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &fake));
    ret.interrupt() = std::move(fake);

    completer.Reply(fit::ok(std::move(ret)));
  }

 protected:
  std::shared_ptr<zx_device> root_;
  component::OutgoingDirectory outgoing_ = component::OutgoingDirectory::Create(dispatcher());
};

TEST_F(Dfv1Test, TestGetInterrupt) {
  sync_completion_t done;
  std::thread spare([this, &done]() {
    ASSERT_EQ(ZX_OK, fragment_irq::GetInterrupt(root_.get(), 1).status_value());
    sync_completion_signal(&done);
  });

  do {
    RunLoopUntilIdle();
  } while (sync_completion_wait(&done, ZX_TIME_INFINITE_PAST) != ZX_OK);

  spare.join();
}

TEST_F(Dfv1Test, TestGetInterruptBadFragment) {
  sync_completion_t done;
  std::thread spare([this, &done]() {
    ASSERT_NE(ZX_OK, fragment_irq::GetInterrupt(root_.get(), 0u).status_value());
    sync_completion_signal(&done);
  });

  do {
    RunLoopUntilIdle();
  } while (sync_completion_wait(&done, ZX_TIME_INFINITE_PAST) != ZX_OK);

  spare.join();
}
