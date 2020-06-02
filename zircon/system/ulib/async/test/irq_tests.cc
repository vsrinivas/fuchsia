// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async/cpp/irq.h>
#include <lib/zx/interrupt.h>

#include <zxtest/zxtest.h>

namespace {

class MockDispatcher : public async::DispatcherStub {
 public:
  zx_status_t BindIrq(async_irq_t* irq) override {
    last_bound_irq = irq;
    return ZX_OK;
  }

  zx_status_t UnbindIrq(async_irq_t* irq) override {
    last_unbound_irq = irq;
    return ZX_OK;
  }
  async_irq_t* last_bound_irq = nullptr;
  async_irq_t* last_unbound_irq = nullptr;
};

TEST(IrqTests, bind_irq_test) {
  MockDispatcher dispatcher;
  zx::interrupt irq_object;
  ASSERT_EQ(ZX_OK, zx::interrupt::create(zx::resource(0), 0, ZX_INTERRUPT_VIRTUAL, &irq_object));
  async::Irq irq;
  irq.set_object(irq_object.get());
  bool triggered = false;
  zx_packet_interrupt_t packet;
  irq.set_handler([&](async_dispatcher_t* dispatcher_arg, async::Irq* irq_arg, zx_status_t status,
                      const zx_packet_interrupt_t* interrupt) {
    triggered = true;
    EXPECT_EQ(&dispatcher, dispatcher_arg);
    EXPECT_EQ(irq_arg, &irq);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(&packet, interrupt);
  });
  EXPECT_EQ(ZX_OK, irq.Begin(&dispatcher));
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, irq.Begin(&dispatcher));
  EXPECT_EQ(irq_object.get(), dispatcher.last_bound_irq->object);
  dispatcher.last_bound_irq->handler(&dispatcher, dispatcher.last_bound_irq, ZX_OK, &packet);
  EXPECT_TRUE(triggered);
  triggered = false;
  EXPECT_EQ(ZX_OK, irq.Cancel());
  EXPECT_EQ(ZX_ERR_NOT_FOUND, irq.Cancel());
  EXPECT_EQ(irq_object.get(), dispatcher.last_unbound_irq->object);
  dispatcher.last_unbound_irq->handler(&dispatcher, dispatcher.last_unbound_irq, ZX_OK, &packet);
  EXPECT_TRUE(triggered);
}

TEST(IrqTests, unsupported_bind_irq_test) {
  async::DispatcherStub dispatcher;
  async_irq_t irq{};
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_bind_irq(&dispatcher, &irq), "valid args");
}
TEST(IrqTests, unsupported_unbind_irq_test) {
  async::DispatcherStub dispatcher;
  async_irq_t irq{};
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_unbind_irq(&dispatcher, &irq), "valid args");
}

}  // namespace
