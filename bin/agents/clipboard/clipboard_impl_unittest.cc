
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "peridot/bin/agents/clipboard/clipboard_impl.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace modular {

class ClipboardImplTest : public gtest::TestWithMessageLoop {
 protected:
  void Push(const std::string& text) { clipboard_.Push(text); }

  void Peek(const Clipboard::PeekCallback& callback) {
    clipboard_.Peek(callback);
  }

  ClipboardImpl clipboard_;
};

namespace {

TEST_F(ClipboardImplTest, FirstPeek) {
  bool callback_called = false;
  Peek([&callback_called](const fidl::String& text) {
    EXPECT_EQ("", text);
    callback_called = true;
  });
  EXPECT_TRUE(callback_called);
}

TEST_F(ClipboardImplTest, PushAndPeek) {
  bool callback_called = false;
  std::string expected_value = "a test string";
  Push(expected_value);
  Peek([&callback_called, expected_value](const fidl::String& text) {
    EXPECT_EQ(expected_value, text);
    callback_called = true;
  });
  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace modular
