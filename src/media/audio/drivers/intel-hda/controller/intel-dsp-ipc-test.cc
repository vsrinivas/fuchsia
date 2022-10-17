// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-ipc.h"

#include <lib/ddk/debug.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include <zxtest/zxtest.h>

#include "debug-logging.h"

namespace audio::intel_hda {
namespace {

// A simple C11 thread wrapper.
class Thread {
 public:
  Thread(std::function<void()> start) : start_(std::move(start)) {
    int result = thrd_create(
        &thread_,
        [](void* start_ptr) {
          auto start = static_cast<std::function<void()>*>(start_ptr);
          (*start)();
          return 0;
        },
        &start_);
    ZX_ASSERT(result == thrd_success);
  }

  void Join() {
    ZX_ASSERT(!joined_);
    int result;
    thrd_join(thread_, &result);
    joined_ = true;
  }

  ~Thread() {
    if (!joined_) {
      Join();
    }
  }

  // Disallow copy/move.
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;

 private:
  bool joined_ = false;
  std::function<void()> start_;
  thrd_t thread_;
};

// Wait for a condition to be set, using exponential backoff.
template <typename F>
void WaitForCond(F cond) {
  zx::duration delay = zx::usec(1);
  while (!cond()) {
    zx::nanosleep(zx::deadline_after(delay));
    delay *= 2;
  }
}

// Simulate the hardware sending an IPC reply, and firing an interrupt.
void SendReply(DspChannel* dsp, MMIO_PTR adsp_registers_t* regs, const IpcMessage& reply) {
  // Send the reply.
  REG_WR(&regs->hipct, reply.primary | ADSP_REG_HIPCT_BUSY | (1 << IPC_PRI_RSP_SHIFT));
  REG_WR(&regs->hipcte, reply.extension);
  REG_SET_BITS(&regs->adspis, ADSP_REG_ADSPIC_IPC);  // Indicate IPC reply ready.
  dsp->ProcessIrq();
}

// Simulate the hardware sending an IPC notification, and firing an interrupt.
void SendNotification(DspChannel* dsp, MMIO_PTR adsp_registers_t* regs, NotificationType type) {
  REG_WR(&regs->hipct, static_cast<uint8_t>(MsgTarget::FW_GEN_MSG) << IPC_PRI_MSG_TGT_SHIFT |
                           static_cast<uint8_t>(MsgDir::MSG_NOTIFICATION) << IPC_PRI_RSP_SHIFT |
                           static_cast<uint8_t>(GlobalType::NOTIFICATION) << IPC_PRI_TYPE_SHIFT |
                           (static_cast<uint16_t>(type) << IPC_PRI_NOTIF_TYPE_SHIFT) |
                           ADSP_REG_HIPCT_BUSY);
  REG_WR(&regs->hipcte, 0U);
  REG_SET_BITS(&regs->adspis, ADSP_REG_ADSPIC_IPC);  // Indicate IPC ready.
  dsp->ProcessIrq();
}

// Poll the IPC-related registers until a message is sent by the driver.
IpcMessage ReadMessage(MMIO_PTR adsp_registers_t* regs) {
  // Wait for a message.
  WaitForCond([&]() { return (REG_RD(&regs->hipci) & ADSP_REG_HIPCI_BUSY) != 0; });

  // Read it.
  uint32_t primary = REG_RD(&regs->hipci);
  uint32_t extension = REG_RD(&regs->hipcie);

  // Clear the busy bit.
  REG_CLR_BITS(&regs->hipci, ADSP_REG_HIPCI_BUSY);

  return IpcMessage(primary, extension);
}

TEST(Ipc, ConstructDestruct) {
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel(
      "UnitTests", FakeMmioPtr(&regs), std::nullopt, zx::duration::infinite());
}

TEST(Ipc, SimpleSend) {
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel(
      "UnitTests", FakeMmioPtr(&regs), std::nullopt, zx::duration::infinite());

  // Start a thread, give it a chance to run.
  auto worker = Thread([&]() {
    zx::result result = dsp->Send(0xaa, 0x55);
    ZX_ASSERT_MSG(result.is_ok(), "Send failed: %s (code: %d)", result.status_string(),
                  result.status_value());
  });

  // Simulate the DSP reading the message.
  IpcMessage message = ReadMessage(FakeMmioPtr(&regs));
  EXPECT_EQ(message.primary & IPC_PRI_MODULE_ID_MASK, 0xaa);
  EXPECT_EQ(message.extension & IPC_EXT_DATA_OFF_SIZE_MASK, 0x55);

  // Ensure the thread remains blocked on the send, even if we wait a little.
  zx::nanosleep(zx::deadline_after(zx::msec(10)));
  EXPECT_TRUE(dsp->IsOperationPending());

  // Simulate the DSP sending a successful reply.
  SendReply(dsp.get(), FakeMmioPtr(&regs), IpcMessage(0, 0));
}

TEST(Ipc, ErrorReply) {
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel(
      "UnitTests", FakeMmioPtr(&regs), std::nullopt, zx::duration::infinite());

  // Start a thread.
  auto worker = Thread([&]() {
    zx::result result = dsp->Send(0xaa, 0x55);
    ZX_ASSERT_MSG(result.status_value() == ZX_ERR_INTERNAL, "Got incorrect error message: %s",
                  result.status_string());
  });

  // Read (and ignore) the message.
  (void)ReadMessage(FakeMmioPtr(&regs));

  // Simulate the DSP sending an error reply, with an arbitrary error code (42).
  //
  // The test will abort if the child thread gets the wrong error code.
  SendReply(dsp.get(), FakeMmioPtr(&regs), IpcMessage(42, 0));
}

TEST(Ipc, HardwareTimeout) {
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp =
      CreateHardwareDspChannel("UnitTests", FakeMmioPtr(&regs), std::nullopt, zx::usec(1));

  // Start a thread.
  auto worker = Thread([&]() {
    zx::result result = dsp->Send(0xaa, 0x55);
    ZX_ASSERT_MSG(result.status_value() == ZX_ERR_TIMED_OUT, "Got incorrect error: %s",
                  result.status_string());
  });

  // Wait for it to time out.
  worker.Join();
}

TEST(Ipc, UnsolicitedReply) {
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel(
      "UnitTests", FakeMmioPtr(&regs), std::nullopt, zx::duration::infinite());

  // Ensuring sending a reply and an IRQ doesn't crash.
  SendReply(dsp.get(), FakeMmioPtr(&regs), IpcMessage(42, 0));
}

TEST(Ipc, QueuedMessages) {
  constexpr int kNumThreads = 10;
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel(
      "UnitTests", FakeMmioPtr(&regs), std::nullopt, zx::duration::infinite());

  // Start many threads, racing to send messages.
  std::array<std::unique_ptr<Thread>, kNumThreads> workers;
  for (int i = 0; i < kNumThreads; i++) {
    workers[i] = std::make_unique<Thread>([i, &dsp]() {
      zx::result result = dsp->Send(0, i);
      ZX_ASSERT_MSG(result.is_ok(), "Send failed: %s (code: %d)", result.status_string(),
                    result.status_value());
    });
  }

  // Simulate the DSP reading off the messages one by one, and ensure that
  // we got all the messages.
  std::unordered_set<int> seen_messages{};
  for (int i = 0; i < kNumThreads; i++) {
    IpcMessage message = ReadMessage(FakeMmioPtr(&regs));
    EXPECT_TRUE(seen_messages.find(message.extension) == seen_messages.end());
    seen_messages.insert(message.extension);
    SendReply(dsp.get(), FakeMmioPtr(&regs), IpcMessage(0, 0));
  }
}

TEST(Ipc, ShutdownWithQueuedSend) {
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel(
      "UnitTests", FakeMmioPtr(&regs), std::nullopt, zx::duration::infinite());

  // Start a thread, and wait for it to send.
  Thread thread([&dsp]() {
    zx::result result = dsp->Send(0, 0);
    ZX_ASSERT_MSG(!result.is_ok() && result.status_value() == ZX_ERR_CANCELED,
                  "Expected send to fail with 'ZX_ERR_CANCELED', but got: %s",
                  result.status_string());
  });
  WaitForCond([&]() { return dsp->IsOperationPending(); });

  // Shut down the IPC object.
  dsp->Shutdown();
}

TEST(Ipc, NotificationNoReceiver) {
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel(
      "UnitTests", FakeMmioPtr(&regs), std::nullopt, zx::duration::infinite());

  // Ensure notifications without a receiver don't crash.
  for (int i = 0; i < 10; i++) {
    SendNotification(dsp.get(), FakeMmioPtr(&regs), NotificationType::FW_READY);
  }
}

TEST(Ipc, NotificationReceived) {
  std::optional<NotificationType> received_notification;
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel(
      "UnitTests", FakeMmioPtr(&regs), [&](NotificationType type) { received_notification = type; },
      zx::duration::infinite());

  // Ensure a notification is sent and received.
  SendNotification(dsp.get(), FakeMmioPtr(&regs), NotificationType::FW_READY);
  EXPECT_EQ(received_notification, NotificationType::FW_READY);
}

TEST(Ipc, SendBigData) {
  adsp_registers_t regs = {};
  std::unique_ptr<DspChannel> dsp = CreateHardwareDspChannel("UnitTests", FakeMmioPtr(&regs));

  // Create a large amount of data.
  std::vector<uint8_t> data;
  data.resize(1'000'000);

  // Ensure we get a valid error trying to send it.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, dsp->SendWithData(0, 0, data, {}, nullptr).status_value());
}

}  // namespace
}  // namespace audio::intel_hda
