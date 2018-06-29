
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/agents/clipboard/clipboard_impl.h"

#include <string>

#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {

class ClipboardImplTest : public testing::TestWithLedger {
 public:
  ClipboardImplTest() = default;

  void SetUp() override {
    TestWithLedger::SetUp();

    clipboard_.reset(new ClipboardImpl(ledger_client()));
  }

  void TearDown() override {
    clipboard_.reset();

    TestWithLedger::TearDown();
  }

 protected:
  void Push(const std::string& text) { clipboard_->Push(text); }

  void Peek(const fuchsia::modular::Clipboard::PeekCallback& callback) {
    clipboard_->Peek(callback);
  }

  std::unique_ptr<ClipboardImpl> clipboard_;
};

namespace {

TEST_F(ClipboardImplTest, FirstPeek) {
  bool callback_called = false;
  Peek([&callback_called](const fidl::StringPtr& text) {
    EXPECT_EQ("", text);
    callback_called = true;
  });

  RunLoopWithTimeoutOrUntil([&callback_called] { return callback_called; });
}

TEST_F(ClipboardImplTest, PushAndPeek) {
  bool callback_called = false;
  std::string expected_value = "a test string";
  Push(expected_value);
  Peek([&callback_called, expected_value](const fidl::StringPtr& text) {
    EXPECT_EQ(expected_value, text);
    callback_called = true;
  });

  RunLoopWithTimeoutOrUntil([&callback_called] { return callback_called; });
}

TEST_F(ClipboardImplTest, PushAndPeekTwice) {
  int callback_called = 0;
  std::string expected_value = "a test string";
  Push(expected_value);
  Peek([&callback_called, expected_value](const fidl::StringPtr& text) {
    EXPECT_EQ(expected_value, text);
    callback_called++;
  });
  Peek([&callback_called, expected_value](const fidl::StringPtr& text) {
    EXPECT_EQ(expected_value, text);
    callback_called++;
  });

  RunLoopWithTimeoutOrUntil(
      [&callback_called] { return callback_called == 2; });
}

}  // namespace
}  // namespace modular
