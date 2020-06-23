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

#include <fidl/test/coding/llcpp/fidl.h>
#include <unittest/unittest.h>

#include "fidl_coded_types.h"

namespace {

class Transaction : public fidl::Transaction {
 public:
  Transaction() = default;
  explicit Transaction(sync_completion_t* wait, sync_completion_t* signal)
      : wait_(wait),
        signal_(signal) {}

  std::unique_ptr<fidl::Transaction> TakeOwnership() override { ZX_ASSERT(false); }

  void Reply(fidl::Message message) override {
    if (wait_ && signal_) {
      sync_completion_signal(signal_);
      sync_completion_wait(wait_, ZX_TIME_INFINITE);
    }
  }

  void Close(zx_status_t epitaph) override {}

  ~Transaction() override = default;

 private:
  sync_completion_t* wait_;
  sync_completion_t* signal_;
};

using Completer = ::llcpp::fidl::test::coding::Llcpp::Interface::ActionCompleter::Sync;

// A completer being destroyed without replying (but needing one) should crash
bool no_reply_asserts() {
  BEGIN_TEST;

  Transaction txn{};
  ASSERT_DEATH([](void* arg) { Completer completer(static_cast<Transaction*>(arg)); }, &txn,
               "no reply should crash");

  END_TEST;
}

// A completer being destroyed without replying (but needing one) should crash
bool no_expected_reply_doesnt_assert() {
  BEGIN_TEST;

  Transaction txn{};
  fidl::Completer<fidl::CompleterBase>::Sync completer(&txn);

  END_TEST;
}

// A completer replying twice should crash
bool double_reply_asserts() {
  BEGIN_TEST;

  Transaction txn{};
  Completer completer(&txn);
  completer.Reply(0);
  ASSERT_DEATH([](void* completer) { static_cast<Completer*>(completer)->Reply(1); }, &completer,
               "second reply should crash");

  END_TEST;
}

// It is allowed to reply and then close
bool reply_then_close_doesnt_assert() {
  BEGIN_TEST;

  Transaction txn{};
  Completer completer(&txn);
  completer.Reply(0);
  completer.Close(ZX_ERR_INVALID_ARGS);

  END_TEST;
}

// It is not allowed to close then reply
bool close_then_reply_asserts() {
  BEGIN_TEST;

  Transaction txn{};
  Completer completer(&txn);
  completer.Close(ZX_ERR_INVALID_ARGS);
  ASSERT_DEATH([](void* completer) { static_cast<Completer*>(completer)->Reply(1); }, &completer,
               "reply after close should crash");

  END_TEST;
}

// It is not allowed to be accessed from multiple threads simultaneously
bool concurrent_access_asserts() {
  BEGIN_TEST;

  sync_completion_t signal, wait;
  Transaction txn{&signal, &wait};
  Completer completer(&txn);
  std::thread t([&] { completer.Reply(1); });
  sync_completion_wait(&wait, ZX_TIME_INFINITE);
  ASSERT_DEATH([](void* completer) { static_cast<Completer*>(completer)->Reply(1); }, &completer,
               "concurrent access should crash");
  ASSERT_DEATH([](void* completer) { static_cast<Completer*>(completer)->Close(ZX_OK); },
               &completer, "concurrent access should crash");
  ASSERT_DEATH([](void* completer) { static_cast<Completer*>(completer)->EnableNextDispatch(); },
               &completer, "concurrent access should crash");
  ASSERT_DEATH([](void* completer) { static_cast<Completer*>(completer)->ToAsync(); },
               &completer, "concurrent access should crash");
  sync_completion_signal(&signal);
  t.join();  // Don't accidentally invoke ~Completer() while `t` is still in Reply().

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(llcpp_transaction)
RUN_TEST(no_reply_asserts)
RUN_TEST(no_expected_reply_doesnt_assert)
RUN_TEST(double_reply_asserts)
RUN_TEST(reply_then_close_doesnt_assert)
RUN_TEST(close_then_reply_asserts)
RUN_TEST(concurrent_access_asserts)
END_TEST_CASE(llcpp_transaction)
