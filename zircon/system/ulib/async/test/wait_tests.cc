// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/defer.h>

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

  // DispatcherStub

  zx_status_t BeginWait(async_wait_t* wait) override {
    ZX_ASSERT(wait->object == dummy_handle);
    // Using an invalid handle would cause a policy exception.
    ZX_ASSERT_MSG(wait->object != last_dummy_handle_deleted,
                  "BeginWait() called with already-deleted object");
    last_op = Op::BEGIN_WAIT;
    last_wait = wait;
    return next_status;
  }

  zx_status_t CancelWait(async_wait_t* wait) override {
    ZX_ASSERT(wait->object == dummy_handle);
    // Using an invalid handle would cause a policy exception.
    ZX_ASSERT_MSG(wait->object != last_dummy_handle_deleted,
                  "CancelWait() called with already-deleted object");
    last_op = Op::CANCEL_WAIT;
    last_wait = wait;
    return next_status;
  }

  // Test utilities
  Op last_op = Op::NONE;
  async_wait_t* last_wait = nullptr;
  zx_handle_t last_dummy_handle_deleted = ZX_HANDLE_INVALID;
  zx_status_t next_status = ZX_OK;
};

class Harness {
 public:
  explicit Harness(MockDispatcher* mock_dispatcher) : mock_dispatcher_(mock_dispatcher) { Reset(); }
  virtual ~Harness() = default;

  void Reset() {
    handler_ran = false;
    handler_deleted = false;
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

  // Unaffected by Reset().
  MockDispatcher* mock_dispatcher_ = nullptr;

  // Reset() during construction, and when Reset() is called separately.
  bool handler_ran;
  bool handler_deleted;
  async::WaitBase* last_wait;
  zx_status_t last_status;
  const zx_packet_signal_t* last_signal;
};

class LambdaHarness : public Harness {
 public:
  // The on_handler_destruct simulates the dummy_handle/object being deleted as soon as the
  // handler is deleted (which is the soonest it should ever be deleted), and notes that the
  // handler has actually been deleted (not just moved somewhere else).
  LambdaHarness(MockDispatcher* mock_dispatcher, zx_handle_t object = ZX_HANDLE_INVALID,
                zx_signals_t trigger = ZX_SIGNAL_NONE, uint32_t options = 0)
      : Harness(mock_dispatcher),
        wait_{object, trigger, options,
              [this, on_handler_destruct = fit::defer([this] {
                       mock_dispatcher_->last_dummy_handle_deleted = dummy_handle;
                       handler_deleted = true;
                     })](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
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
  LambdaOnceHarness(MockDispatcher* mock_dispatcher, zx_handle_t object = ZX_HANDLE_INVALID,
                    zx_signals_t trigger = ZX_SIGNAL_NONE, uint32_t options = 0)
      : Harness(mock_dispatcher), wait_{object, trigger, options} {}

  async::WaitBase& wait() override { return wait_; }
  bool wait_has_handler() override { return !handler_ran; }
  bool wait_retains_handler() override { return false; }

  zx_status_t BeginWait(async_dispatcher_t* dispatcher) override {
    // The on_handler_destruct simulates the dummy_handle/object being deleted as soon as the
    // handler is deleted (which is the soonest it should ever be deleted), and notes that the
    // handler has actually been deleted (not just moved somewhere else).
    //
    // We only simulate this if there's not already a BeginWait() in progress.
    //
    // While it's true that if a client calls async::WaitOnce::Begin() on a WaitOnce instance that
    // already has a wait in progress, passing in a handler which when deleted will delete the
    // same object as the handler for the wait that's already in progress, that's a client bug.
    // If a client really needed to attempt a second BeginWait() (it really doesn't need to), then
    // it could co-own instead of double-own the object in each of two handlers, where the second
    // handler's immediate deletion during failed BeginWait() would just de-ref, not actually
    // delete.
    auto on_handler_destruct = fit::defer([this, was_fresh_wait = !wait_.is_pending()] {
      if (was_fresh_wait) {
        mock_dispatcher_->last_dummy_handle_deleted = dummy_handle;
        handler_deleted = true;
      }
    });
    return wait_.Begin(dispatcher, [this, on_handler_destruct = std::move(on_handler_destruct)](
                                       async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                       zx_status_t status, const zx_packet_signal_t* signal) {
      Handler(dispatcher, wait, status, signal);
    });
  }

 private:
  async::WaitOnce wait_;
};

class MethodHarness : public Harness {
 public:
  MethodHarness(MockDispatcher* mock_dispatcher, zx_handle_t object = ZX_HANDLE_INVALID,
                zx_signals_t trigger = ZX_SIGNAL_NONE, uint32_t options = 0)
      : Harness(mock_dispatcher), wait_{this, object, trigger, options} {}

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
  MockDispatcher dispatcher;
  Harness harness(&dispatcher);

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
    Harness harness(&dispatcher, dummy_handle, dummy_trigger, dummy_options);
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

  // Pretend like we're using a new handle and happen to get dummy_handle value
  // again.
  dispatcher.last_dummy_handle_deleted = ZX_HANDLE_INVALID;

  {
    Harness harness(&dispatcher, dummy_handle, dummy_trigger, dummy_options);
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
    Harness harness(&dispatcher, dummy_handle, dummy_trigger, dummy_options);
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
    Harness harness(&dispatcher, dummy_handle, dummy_trigger, dummy_options);
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
    EXPECT_EQ(!harness.wait_retains_handler(), harness.handler_deleted);
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
