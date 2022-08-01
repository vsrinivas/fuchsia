// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.coding.fuchsia/cpp/wire.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <lib/sync/completion.h>
#include <limits.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <thread>
#include <type_traits>

#include <zxtest/zxtest.h>

namespace {

class Transaction : public fidl::Transaction {
 public:
  Transaction() = default;
  explicit Transaction(sync_completion_t* wait, sync_completion_t* signal)
      : wait_(wait), signal_(signal) {}

  std::unique_ptr<fidl::Transaction> TakeOwnership() override { ZX_ASSERT(false); }

  zx_status_t Reply(fidl::OutgoingMessage* message, fidl::WriteOptions write_options) override {
    if (wait_ && signal_) {
      sync_completion_signal(signal_);
      sync_completion_wait(wait_, ZX_TIME_INFINITE);
    }
    return ZX_OK;
  }

  void Close(zx_status_t epitaph) override {}

  void InternalError(fidl::UnbindInfo error, fidl::ErrorOrigin origin) override {
    error_.emplace(error);
    fidl::Transaction::InternalError(error, origin);
  }

  ~Transaction() override = default;

  const std::optional<fidl::UnbindInfo>& error() const { return error_; }

 private:
  sync_completion_t* wait_;
  sync_completion_t* signal_;
  std::optional<fidl::UnbindInfo> error_ = std::nullopt;
};

using OneWayCompleter =
    fidl::WireServer<::fidl_test_coding_fuchsia::Example>::OneWayCompleter::Sync;

TEST(LlcppTransaction, one_way_completer_reply_not_needed) {
  Transaction txn{};
  OneWayCompleter completer(&txn);
  EXPECT_FALSE(completer.is_reply_needed());
}

using Completer = fidl::WireServer<::fidl_test_coding_fuchsia::Llcpp>::ActionCompleter::Sync;

// A completer being destroyed without replying (but needing one) should crash
TEST(LlcppTransaction, no_reply_asserts) {
  Transaction txn{};
  ASSERT_DEATH([&] { Completer completer(&txn); }, "no reply should crash");
}

// A completer being destroyed without replying (but needing one) should crash
TEST(LlcppTransaction, no_expected_reply_doesnt_assert) {
  Transaction txn{};
  fidl::Completer<fidl::CompleterBase>::Sync completer(&txn);
}

// A completer replying twice should crash
TEST(LlcppTransaction, double_reply_asserts) {
  Transaction txn{};
  Completer completer(&txn);
  completer.Reply(0);
  ASSERT_DEATH([&] { completer.Reply(1); }, "second reply should crash");
}

// It is allowed to reply and then close
TEST(LlcppTransaction, reply_then_close_doesnt_assert) {
  Transaction txn{};
  Completer completer(&txn);
  EXPECT_TRUE(completer.is_reply_needed());
  completer.Reply(0);
  EXPECT_FALSE(completer.is_reply_needed());
  completer.Close(ZX_ERR_INVALID_ARGS);
  EXPECT_FALSE(completer.is_reply_needed());
}

// It is not allowed to close then reply
TEST(LlcppTransaction, close_then_reply_asserts) {
  Transaction txn{};
  Completer completer(&txn);
  EXPECT_TRUE(completer.is_reply_needed());
  completer.Close(ZX_ERR_INVALID_ARGS);
  EXPECT_FALSE(completer.is_reply_needed());
  ASSERT_DEATH([&] { completer.Reply(1); }, "reply after close should crash");
}

// It is not allowed to be accessed from multiple threads simultaneously
TEST(LlcppTransaction, concurrent_access_asserts) {
  sync_completion_t signal, wait;
  Transaction txn{&signal, &wait};
  Completer completer(&txn);
  std::thread t([&] { completer.Reply(1); });
  sync_completion_wait(&wait, ZX_TIME_INFINITE);
  // TODO(fxbug.dev/54499): Hide assertion failed messages from output - they are confusing.
  ASSERT_DEATH([&] { completer.Reply(1); }, "concurrent access should crash");
  ASSERT_DEATH([&] { completer.Close(ZX_OK); }, "concurrent access should crash");
  ASSERT_DEATH([&] { completer.EnableNextDispatch(); }, "concurrent access should crash");
  ASSERT_DEATH([&] { completer.ToAsync(); }, "concurrent access should crash");
  sync_completion_signal(&signal);
  t.join();  // Don't accidentally invoke ~Completer() while `t` is still in Reply().
}

// If there is a serialization error, it does not need to be closed or replied to.
TEST(LlcppTransaction, transaction_error) {
  Transaction txn{};
  fidl::WireServer<::fidl_test_coding_fuchsia::Llcpp>::EnumActionCompleter::Sync completer(&txn);
  // We are using the fact that 2 isn't a valid enum value to cause an error.
  EXPECT_FALSE(txn.error().has_value());
  completer.Reply(static_cast<fidl_test_coding_fuchsia::wire::TestEnum>(2));
  EXPECT_TRUE(txn.error().has_value());
  EXPECT_EQ(fidl::Reason::kEncodeError, txn.error()->reason());
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, txn.error()->status());
}

TEST(CompleterResultOfReply, CalledWithoutMakingAReply) {
  Transaction txn{};
  Completer completer(&txn);
  ASSERT_DEATH([&] { (void)completer.result_of_reply(); });
  // Passivate the completer.
  completer.Close(ZX_OK);
}

TEST(CompleterResultOfReply, Ok) {
  Transaction txn{};
  Completer completer(&txn);
  completer.Reply(0);
  EXPECT_OK(completer.result_of_reply().status());
}

TEST(CompleterResultOfReply, EncodeError) {
  Transaction txn{};
  fidl::WireServer<::fidl_test_coding_fuchsia::Llcpp>::EnumActionCompleter::Sync completer(&txn);
  // We are using the fact that 2 isn't a valid enum value to cause an error.
  EXPECT_FALSE(txn.error().has_value());
  completer.Reply(static_cast<fidl_test_coding_fuchsia::wire::TestEnum>(2));
  fidl::Status result = completer.result_of_reply();
  EXPECT_EQ(fidl::Reason::kEncodeError, result.reason());
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, result.status());
}

TEST(CompleterResultOfReply, TransportError) {
  class FakeTransportErrorTransaction : public fidl::Transaction {
    std::unique_ptr<Transaction> TakeOwnership() override { ZX_PANIC("Unused"); }

    zx_status_t Reply(fidl::OutgoingMessage* message,
                      fidl::WriteOptions write_options = {}) override {
      return ZX_ERR_ACCESS_DENIED;
    }

    void Close(zx_status_t epitaph) override {}
  };

  FakeTransportErrorTransaction txn{};
  Completer completer(&txn);
  completer.Reply(0);
  fidl::Status result = completer.result_of_reply();
  EXPECT_EQ(fidl::Reason::kTransportError, result.reason());
  EXPECT_STATUS(ZX_ERR_ACCESS_DENIED, result.status());
}

namespace test_async_completer_deleted_methods {

template <typename T, typename = void>
struct test : std::false_type {};
template <typename T>
struct test<T, std::void_t<decltype(std::declval<T>().EnableNextDispatch())>> : std::true_type {};

// Invoking `FooCompleter::Async::EnableNextDispatch` should be a compile-time error.
TEST(LlcppCompleter, AsyncCompleterCannotEnableNextDispatch) {
  Transaction txn{};
  Completer completer(&txn);
  static_assert(test<decltype(completer)>::value);
  static_assert(!test<decltype(completer.ToAsync())>::value);

  // Not relevant to the test, but required to neutralize the completer.
  completer.Close(ZX_OK);
}

}  // namespace test_async_completer_deleted_methods

namespace test_sync_completer_deleted_methods {

template <typename T>
T TryToMove(T t) {
  return std::move(t);
}

template <typename T, typename = void>
struct test : std::false_type {};
template <typename T>
struct test<T, std::void_t<decltype(TryToMove<T>(std::declval<T>()))>> : std::true_type {};

// Invoking move construction on `FooCompleter::Sync` should be a compile-time error.
TEST(LlcppCompleter, SyncCompleterCannotBeMoved) {
  Transaction txn{};
  Completer completer(&txn);

  // Sync one cannot be moved.
  static_assert(!test<decltype(completer)>::value);

  // Async one can be moved.
  static_assert(test<decltype(completer.ToAsync())>::value);

  // Not relevant to the test, but required to neutralize the completer.
  completer.Close(ZX_OK);
}

}  // namespace test_sync_completer_deleted_methods

}  // namespace
