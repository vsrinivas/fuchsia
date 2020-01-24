// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/transaction.h>
#include <limits.h>
#include <zircon/syscalls.h>

#include <fidl/test/coding/llcpp/fidl.h>
#include <unittest/unittest.h>

#include "fidl_coded_types.h"

namespace {

class Transaction : public fidl::Transaction {
 public:
  Transaction() = default;

  std::unique_ptr<fidl::Transaction> TakeOwnership() override { ZX_ASSERT(false); }

  void Reply(fidl::Message message) override {}

  void Close(zx_status_t epitaph) override {}

  ~Transaction() override = default;
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

}  // namespace

BEGIN_TEST_CASE(llcpp_transaction)
RUN_TEST(no_reply_asserts)
RUN_TEST(no_expected_reply_doesnt_assert)
RUN_TEST(double_reply_asserts)
RUN_TEST(reply_then_close_doesnt_assert)
RUN_TEST(close_then_reply_asserts)
END_TEST_CASE(llcpp_transaction)
