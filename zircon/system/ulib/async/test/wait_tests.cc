// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async/cpp/wait.h>

#include <zxtest/zxtest.h>

namespace {

const zx_handle_t dummy_handle = static_cast<zx_handle_t>(1);
const zx_signals_t dummy_trigger = ZX_USER_SIGNAL_0;
const uint32_t dummy_options = 0x55;
const zx_packet_signal_t dummy_signal{.trigger = dummy_trigger,
                                      .observed = ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1,
                                      .count = 0u,
                                      .timestamp = 0u,
                                      .reserved1 = 0u};

class MockDispatcher : public async::DispatcherStub {
 public:
  enum class Op {
    NONE,
    BEGIN_WAIT,
    CANCEL_WAIT,
  };

  zx_status_t BeginWait(async_wait_t* wait) override {
    last_op = Op::BEGIN_WAIT;
    last_wait = wait;
    return next_status;
  }

  zx_status_t CancelWait(async_wait_t* wait) override {
    last_op = Op::CANCEL_WAIT;
    last_wait = wait;
    return next_status;
  }

  Op last_op = Op::NONE;
  async_wait_t* last_wait = nullptr;
  zx_status_t next_status = ZX_OK;
};

class Harness {
 public:
  Harness() { Reset(); }

  void Reset() {
    handler_ran = false;
    last_wait = nullptr;
    last_status = ZX_ERR_INTERNAL;
    last_signal = nullptr;
  }

  void Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
               const zx_packet_signal_t* signal) {
    handler_ran = true;
    last_wait = wait;
    last_status = status;
    last_signal = signal;
  }

  virtual async::WaitBase& wait() = 0;
  virtual bool wait_has_handler() = 0;
  virtual bool wait_retains_handler() = 0;

  virtual zx_status_t BeginWait(async_dispatcher_t* dispatcher) = 0;

  bool handler_ran;
  async::WaitBase* last_wait;
  zx_status_t last_status;
  const zx_packet_signal_t* last_signal;
};

class LambdaHarness : public Harness {
 public:
  LambdaHarness(zx_handle_t object = ZX_HANDLE_INVALID, zx_signals_t trigger = ZX_SIGNAL_NONE,
                uint32_t options = 0)
      : wait_{object, trigger, options,
              [this](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                     const zx_packet_signal_t* signal) {
                Handler(dispatcher, wait, status, signal);
              }} {}

  async::WaitBase& wait() override { return wait_; }
  bool wait_has_handler() override { return wait_.has_handler(); }
  bool wait_retains_handler() override { return true; }

  zx_status_t BeginWait(async_dispatcher_t* dispatcher) override { return wait_.Begin(dispatcher); }

 private:
  async::Wait wait_;
};

class LambdaOnceHarness : public Harness {
 public:
  LambdaOnceHarness(zx_handle_t object = ZX_HANDLE_INVALID, zx_signals_t trigger = ZX_SIGNAL_NONE,
                    uint32_t options = 0)
      : wait_{object, trigger, options} {}

  async::WaitBase& wait() override { return wait_; }
  bool wait_has_handler() override { return !handler_ran; }
  bool wait_retains_handler() override { return false; }

  zx_status_t BeginWait(async_dispatcher_t* dispatcher) override {
    return wait_.Begin(dispatcher, [this](async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                          zx_status_t status, const zx_packet_signal_t* signal) {
      Handler(dispatcher, wait, status, signal);
    });
  }

 private:
  async::WaitOnce wait_;
};

class MethodHarness : public Harness {
 public:
  MethodHarness(zx_handle_t object = ZX_HANDLE_INVALID, zx_signals_t trigger = ZX_SIGNAL_NONE,
                uint32_t options = 0)
      : wait_{this, object, trigger, options} {}

  async::WaitBase& wait() override { return wait_; }
  bool wait_has_handler() override { return true; }
  bool wait_retains_handler() override { return true; }

  zx_status_t BeginWait(async_dispatcher_t* dispatcher) override { return wait_.Begin(dispatcher); }

 private:
  async::WaitMethod<Harness, &Harness::Handler> wait_;
};

TEST(WaitTests, wait_set_handler_test) {
  {
    async::Wait wait;
    EXPECT_FALSE(wait.has_handler());
    EXPECT_FALSE(wait.is_pending());

    wait.set_handler([](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {});
    EXPECT_TRUE(wait.has_handler());
  }

  {
    async::Wait wait(ZX_HANDLE_INVALID, ZX_SIGNAL_NONE, 0,
                     [](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {});
    EXPECT_TRUE(wait.has_handler());
    EXPECT_FALSE(wait.is_pending());
  }
}

template <typename Harness>
void wait_properties_test() {
  Harness harness;

  EXPECT_EQ(ZX_HANDLE_INVALID, harness.wait().object());
  harness.wait().set_object(dummy_handle);
  EXPECT_EQ(dummy_handle, harness.wait().object());

  EXPECT_EQ(ZX_SIGNAL_NONE, harness.wait().trigger());
  harness.wait().set_trigger(dummy_trigger);
  EXPECT_EQ(dummy_trigger, harness.wait().trigger());

  EXPECT_EQ(0, harness.wait().options());
  harness.wait().set_options(dummy_options);
  EXPECT_EQ(dummy_options, harness.wait().options());
}

template <typename Harness>
void wait_begin_test() {
  MockDispatcher dispatcher;

  {
    Harness harness(dummy_handle, dummy_trigger, dummy_options);
    EXPECT_FALSE(harness.wait().is_pending());

    dispatcher.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, harness.BeginWait(&dispatcher));
    EXPECT_TRUE(harness.wait().is_pending());
    EXPECT_EQ(MockDispatcher::Op::BEGIN_WAIT, dispatcher.last_op);
    EXPECT_EQ(dummy_handle, dispatcher.last_wait->object);
    EXPECT_EQ(dummy_trigger, dispatcher.last_wait->trigger);
    EXPECT_EQ(dummy_options, dispatcher.last_wait->options);
    EXPECT_FALSE(harness.handler_ran);

    harness.Reset();
    dispatcher.last_op = MockDispatcher::Op::NONE;
    EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, harness.BeginWait(&dispatcher));
    EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
    EXPECT_FALSE(harness.handler_ran);
  }
  EXPECT_EQ(MockDispatcher::Op::CANCEL_WAIT, dispatcher.last_op);

  {
    Harness harness(dummy_handle, dummy_trigger, dummy_options);
    EXPECT_FALSE(harness.wait().is_pending());

    dispatcher.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, harness.BeginWait(&dispatcher));
    EXPECT_EQ(MockDispatcher::Op::BEGIN_WAIT, dispatcher.last_op);
    EXPECT_FALSE(harness.wait().is_pending());
    EXPECT_FALSE(harness.handler_ran);
  }
  EXPECT_EQ(MockDispatcher::Op::BEGIN_WAIT, dispatcher.last_op);
}

template <typename Harness>
void wait_cancel_test() {
  MockDispatcher dispatcher;

  {
    Harness harness(dummy_handle, dummy_trigger, dummy_options);
    EXPECT_FALSE(harness.wait().is_pending());

    EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.wait().Cancel());
    EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
    EXPECT_FALSE(harness.wait().is_pending());

    EXPECT_EQ(ZX_OK, harness.BeginWait(&dispatcher));
    EXPECT_EQ(MockDispatcher::Op::BEGIN_WAIT, dispatcher.last_op);
    EXPECT_TRUE(harness.wait().is_pending());

    EXPECT_EQ(ZX_OK, harness.wait().Cancel());
    EXPECT_EQ(MockDispatcher::Op::CANCEL_WAIT, dispatcher.last_op);
    EXPECT_FALSE(harness.wait().is_pending());

    dispatcher.last_op = MockDispatcher::Op::NONE;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.wait().Cancel());
    EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
    EXPECT_FALSE(harness.wait().is_pending());
  }
  EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
}

template <typename Harness>
void wait_run_handler_test() {
  MockDispatcher dispatcher;

  {
    Harness harness(dummy_handle, dummy_trigger, dummy_options);
    EXPECT_FALSE(harness.wait().is_pending());

    EXPECT_EQ(ZX_OK, harness.BeginWait(&dispatcher));
    EXPECT_EQ(MockDispatcher::Op::BEGIN_WAIT, dispatcher.last_op);
    EXPECT_TRUE(harness.wait().is_pending());

    harness.Reset();
    dispatcher.last_wait->handler(&dispatcher, dispatcher.last_wait, ZX_OK, &dummy_signal);
    EXPECT_TRUE(harness.handler_ran);
    EXPECT_EQ(&harness.wait(), harness.last_wait);
    EXPECT_EQ(ZX_OK, harness.last_status);
    EXPECT_EQ(&dummy_signal, harness.last_signal);
    EXPECT_FALSE(harness.wait().is_pending());

    dispatcher.last_op = MockDispatcher::Op::NONE;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, harness.wait().Cancel());
    EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
    EXPECT_FALSE(harness.wait().is_pending());

    EXPECT_EQ(harness.wait_retains_handler(), harness.wait_has_handler());
  }
  EXPECT_EQ(MockDispatcher::Op::NONE, dispatcher.last_op);
}

TEST(WaitTests, unsupported_begin_wait_test) {
  async::DispatcherStub dispatcher;
  async_wait_t wait{};
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_begin_wait(&dispatcher, &wait), "valid args");
}

TEST(WaitTests, unsupported_cancel_wait_test) {
  async::DispatcherStub dispatcher;
  async_wait_t wait{};
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_cancel_wait(&dispatcher, &wait), "valid args");
}

}  // namespace

TEST(WaitTests, wait_properties_test_LambdaHarness) { wait_properties_test<LambdaHarness>(); }

TEST(WaitTests, wait_properties_test_LambdaOnceHarness) {
  wait_properties_test<LambdaOnceHarness>();
}

TEST(WaitTests, wait_properties_test_MethodHarness) { wait_properties_test<MethodHarness>(); }

TEST(WaitTests, wait_begin_test_LambdaHarness) { wait_begin_test<LambdaHarness>(); }

TEST(WaitTests, wait_begin_test_LambdaOnceHarness) { wait_begin_test<LambdaOnceHarness>(); }

TEST(WaitTests, wait_begin_test_MethodHarness) { wait_begin_test<MethodHarness>(); }

TEST(WaitTests, wait_cancel_test_LambdaHarness) { wait_cancel_test<LambdaHarness>(); }

TEST(WaitTests, wait_cancel_test_LambdaOnceHarness) { wait_cancel_test<LambdaOnceHarness>(); }

TEST(WaitTests, wait_cancel_test_MethodHarness) { wait_cancel_test<MethodHarness>(); }

TEST(WaitTests, wait_run_handler_test_LambdaHarness) { wait_run_handler_test<LambdaHarness>(); }

TEST(WaitTests, wait_run_handler_test_LambdaOnceHarness) {
  wait_run_handler_test<LambdaOnceHarness>();
}

TEST(WaitTests, wait_run_handler_test_MethodHarness) { wait_run_handler_test<MethodHarness>(); }
