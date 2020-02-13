// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <byteswap.h>
#include <lib/zx/clock.h>

#include <zxtest/zxtest.h>

#include "../controller.h"
#include "fake-bus.h"

namespace ahci {

class AhciTestFixture : public zxtest::Test {
 public:
 protected:
  void SetUp() override {}
  void TearDown() override;

  zx_status_t PortEnable(Port* port);

  // If non-null, this pointer is owned by Controller::bus_
  std::unique_ptr<FakeBus> fake_bus_;

 private:
};

void AhciTestFixture::TearDown() { fake_bus_.reset(); }

zx_status_t AhciTestFixture::PortEnable(Port* port) {
  zx_device_t* fake_parent = nullptr;
  std::unique_ptr<FakeBus> bus(new FakeBus());
  EXPECT_OK(bus->Configure(fake_parent));

  uint32_t cap;
  EXPECT_OK(bus->RegRead(kHbaCapabilities, &cap));
  EXPECT_OK(port->Configure(0, bus.get(), kHbaPorts, cap));
  EXPECT_OK(port->Enable());

  // Fake detect of device.
  port->set_present(true);

  EXPECT_TRUE(port->is_present());
  EXPECT_TRUE(port->is_implemented());
  EXPECT_TRUE(port->is_valid());
  EXPECT_FALSE(port->is_paused());

  fake_bus_ = std::move(bus);
  return ZX_OK;
}

void string_fix(uint16_t* buf, size_t size);

TEST(SataTest, StringFixTest) {
  // Nothing to do.
  string_fix(nullptr, 0);

  // Zero length, no swapping happens.
  uint16_t a = 0x1234;
  string_fix(&a, 0);
  ASSERT_EQ(a, 0x1234, "unexpected string result");

  // One character, only swap to even lengths.
  a = 0x1234;
  string_fix(&a, 1);
  ASSERT_EQ(a, 0x1234, "unexpected string result");

  // Swap A.
  a = 0x1234;
  string_fix(&a, sizeof(a));
  ASSERT_EQ(a, 0x3412, "unexpected string result");

  // Swap a group of values.
  uint16_t b[] = {0x0102, 0x0304, 0x0506};
  string_fix(b, sizeof(b));
  const uint16_t b_rev[] = {0x0201, 0x0403, 0x0605};
  ASSERT_EQ(memcmp(b, b_rev, sizeof(b)), 0, "unexpected string result");

  // Swap a string.
  const char* qemu_model_id = "EQUMH RADDSI K";
  const char* qemu_rev = "QEMU HARDDISK ";
  const size_t qsize = strlen(qemu_model_id);

  union {
    uint16_t word[10];
    char byte[20];
  } str;

  memcpy(str.byte, qemu_model_id, qsize);
  string_fix(str.word, qsize);
  ASSERT_EQ(memcmp(str.byte, qemu_rev, qsize), 0, "unexpected string result");

  const char* sin = "abcdefghijklmnoprstu";  // 20 chars
  const size_t slen = strlen(sin);
  ASSERT_EQ(slen, 20, "bad string length");
  ASSERT_EQ(slen & 1, 0, "string length must be even");
  char sout[22];
  memset(sout, 0, sizeof(sout));
  memcpy(sout, sin, slen);

  // Verify swapping the length of every pair from 0 to 20 chars, inclusive.
  for (size_t i = 0; i <= slen; i += 2) {
    memcpy(str.byte, sin, slen);
    string_fix(str.word, i);
    ASSERT_EQ(memcmp(str.byte, sout, slen), 0, "unexpected string result");
    ASSERT_EQ(sout[slen], 0, "buffer overrun");
    char c = sout[i];
    sout[i] = sout[i + 1];
    sout[i + 1] = c;
  }
}

TEST(AhciTest, Create) {
  zx_device_t* fake_parent = nullptr;
  std::unique_ptr<FakeBus> bus(new FakeBus());

  std::unique_ptr<Controller> con;
  EXPECT_OK(Controller::CreateWithBus(fake_parent, std::move(bus), &con));
}

TEST(AhciTest, CreateBusConfigFailure) {
  zx_device_t* fake_parent = nullptr;
  std::unique_ptr<FakeBus> bus(new FakeBus());
  bus->DoFailConfigure();

  std::unique_ptr<Controller> con;
  // Expected to fail during bus configure.
  EXPECT_NOT_OK(Controller::CreateWithBus(fake_parent, std::move(bus), &con));
}

TEST(AhciTest, LaunchThreads) {
  zx_device_t* fake_parent = nullptr;
  std::unique_ptr<FakeBus> bus(new FakeBus());

  std::unique_ptr<Controller> con;
  EXPECT_OK(Controller::CreateWithBus(fake_parent, std::move(bus), &con));

  EXPECT_OK(con->LaunchThreads());
  con->Shutdown();
}

TEST(AhciTest, HbaReset) {
  zx_device_t* fake_parent = nullptr;
  std::unique_ptr<FakeBus> bus(new FakeBus());
  std::unique_ptr<Controller> con;
  EXPECT_OK(Controller::CreateWithBus(fake_parent, std::move(bus), &con));

  // Test reset function.
  EXPECT_OK(con->HbaReset());

  con->Shutdown();
}

TEST_F(AhciTestFixture, PortTestEnable) {
  Port port;
  EXPECT_OK(PortEnable(&port));
}

void cb_status(void* cookie, zx_status_t status, block_op_t* bop) {
  *static_cast<zx_status_t*>(cookie) = status;
}

void cb_assert(void* cookie, zx_status_t status, block_op_t* bop) { EXPECT_TRUE(false); }

TEST_F(AhciTestFixture, PortCompleteNone) {
  Port port;
  EXPECT_OK(PortEnable(&port));

  // Complete with no running transactions.

  EXPECT_FALSE(port.Complete());
}

TEST_F(AhciTestFixture, PortCompleteRunning) {
  Port port;
  EXPECT_OK(PortEnable(&port));

  // Complete with running transaction. No completion should occur, cb_assert should not fire.

  sata_txn_t txn = {};
  txn.timeout = zx::clock::get_monotonic() + zx::sec(5);
  txn.completion_cb = cb_assert;

  uint32_t slot = 0;

  // Set txn as running.
  port.TestSetRunning(&txn, slot);
  // Set the running bit in the bus.
  fake_bus_->PortRegOverride(0, kPortSataActive, (1u << slot));

  // Set interrupt for successful transfer completion, but keep the running bit set.
  // Simulates a non-error interrupt that will cause the IRQ handler to examin the running
  // transactions.
  fake_bus_->PortRegOverride(0, kPortInterruptStatus, AHCI_PORT_INT_DP);
  // Invoke interrupt handler.
  port.HandleIrq();

  EXPECT_TRUE(port.Complete());
}

TEST_F(AhciTestFixture, PortCompleteSuccess) {
  Port port;
  EXPECT_OK(PortEnable(&port));

  // Transaction has successfully completed.

  zx_status_t status = 100;  // Bogus value to be overwritten by callback.

  sata_txn_t txn = {};
  txn.timeout = zx::clock::get_monotonic() + zx::sec(5);
  txn.completion_cb = cb_status;
  txn.cookie = &status;

  uint32_t slot = 0;

  // Set txn as running.
  port.TestSetRunning(&txn, slot);
  // Clear the running bit in the bus.
  fake_bus_->PortRegOverride(0, kPortSataActive, 0);

  // Set interrupt for successful transfer completion.
  fake_bus_->PortRegOverride(0, kPortInterruptStatus, AHCI_PORT_INT_DP);
  // Invoke interrupt handler.
  port.HandleIrq();

  // False means no more running commands.
  EXPECT_FALSE(port.Complete());
  // Set by completion callback.
  EXPECT_OK(status);
}

TEST_F(AhciTestFixture, PortCompleteTimeout) {
  Port port;
  EXPECT_OK(PortEnable(&port));

  // Transaction has successfully completed.

  zx_status_t status = ZX_OK;  // Value to be overwritten by callback.

  sata_txn_t txn = {};
  // Set timeout in the past.
  txn.timeout = zx::clock::get_monotonic() - zx::sec(1);
  txn.completion_cb = cb_status;
  txn.cookie = &status;

  uint32_t slot = 0;

  // Set txn as running.
  port.TestSetRunning(&txn, slot);
  // Set the running bit in the bus.
  fake_bus_->PortRegOverride(0, kPortSataActive, (1u << slot));

  // Set interrupt for successful transfer completion.
  fake_bus_->PortRegOverride(0, kPortInterruptStatus, AHCI_PORT_INT_DP);
  // Invoke interrupt handler.
  port.HandleIrq();

  // False means no more running commands.
  EXPECT_FALSE(port.Complete());
  // Set by completion callback.
  EXPECT_NOT_OK(status);
}

}  // namespace ahci
