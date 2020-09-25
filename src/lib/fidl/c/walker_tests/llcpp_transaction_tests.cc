// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/sync/completion.h>
#include <limits.h>
#include <zircon/syscalls.h>

#include <thread>

#include <fidl/test/coding/fuchsia/llcpp/fidl.h>
#include <fidl/test/coding/llcpp/fidl.h>
#include <zxtest/zxtest.h>

#include "fidl_coded_types.h"

namespace {

class Transaction : public fidl::Transaction {
 public:
  Transaction() = default;
  explicit Transaction(sync_completion_t* wait, sync_completion_t* signal)
      : wait_(wait), signal_(signal) {}

  std::unique_ptr<fidl::Transaction> TakeOwnership() override { ZX_ASSERT(false); }

  zx_status_t Reply(fidl::FidlMessage* message) override {
    if (wait_ && signal_) {
      sync_completion_signal(signal_);
      sync_completion_wait(wait_, ZX_TIME_INFINITE);
    }
    return ZX_OK;
  }

  void Close(zx_status_t epitaph) override {}

  ~Transaction() override = default;

 private:
  sync_completion_t* wait_;
  sync_completion_t* signal_;
};

using Completer = ::llcpp::fidl::test::coding::fuchsia::Llcpp::Interface::ActionCompleter::Sync;

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
  completer.Reply(0);
  completer.Close(ZX_ERR_INVALID_ARGS);
}

// It is not allowed to close then reply
TEST(LlcppTransaction, close_then_reply_asserts) {
  Transaction txn{};
  Completer completer(&txn);
  completer.Close(ZX_ERR_INVALID_ARGS);
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
  ::llcpp::fidl::test::coding::fuchsia::Llcpp::Interface::EnumActionCompleter::Sync completer(&txn);
  // We are using the fact that 2 isn't a valid enum value to cause an error.
  fidl::Result result =
      completer.Reply(static_cast<llcpp::fidl::test::coding::fuchsia::TestEnum>(2));
  ASSERT_FALSE(result.ok());
}

}  // namespace
