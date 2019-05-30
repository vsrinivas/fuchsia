// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/message_decoder.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"
#include "test/fidlcat/examples/cpp/fidl.h"
#include "tools/fidlcat/lib/fidlcat_test.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/library_loader_test_data.h"
#include "tools/fidlcat/lib/wire_object.h"
#include "wire_parser.h"

using test::fidlcat::examples::Echo;
using test::fidlcat::examples::FidlcatTestInterface;

namespace fidlcat {

class MessageDecoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loader_ = GetLoader();
    ASSERT_NE(loader_, nullptr);
    display_options_.pretty_print = true;
    display_options_.columns = 80;
  };
  LibraryLoader* loader_;
  std::map<std::tuple<zx_handle_t, uint64_t>, Direction> handle_directions_;
  DisplayOptions display_options_;
  uint64_t process_koid_ = ULLONG_MAX;
  bool read_ = true;
  std::stringstream result_;
};

#define TEST_DECODE_MESSAGE(_interface, _iface, _expected, ...)             \
  do {                                                                      \
    fidl::MessageBuffer buffer;                                             \
    fidl::Message message = buffer.CreateEmptyMessage();                    \
    zx_handle_t handle = ZX_HANDLE_INVALID;                                 \
    InterceptRequest<_interface>(message,                                   \
                                 [&](fidl::InterfacePtr<_interface>& ptr) { \
                                   ptr->_iface(__VA_ARGS__);                \
                                 });                                        \
    DecodeMessage(loader_, &handle_directions_, display_options_,           \
                  process_koid_, handle, message, read_, result_);          \
    ASSERT_EQ(result_.str(), _expected)                                     \
        << "expected = " << _expected << " actual = " << result_.str();     \
  } while (0)

TEST_F(MessageDecoderTest, TestStringLaunched) {
  TEST_DECODE_MESSAGE(
      FidlcatTestInterface, String,
      "request test.fidlcat.examples/FidlcatTestInterface.String = {\n"
      "  s: string = \"Hello World\"\n"
      "}\n",
      "Hello World");
}

TEST_F(MessageDecoderTest, TestStringAttached) {
  process_koid_ = 0x1234;
  TEST_DECODE_MESSAGE(
      FidlcatTestInterface, String,
      "request test.fidlcat.examples/FidlcatTestInterface.String = {\n"
      "  s: string = \"Hello World\"\n"
      "}\n",
      "Hello World");
}

TEST_F(MessageDecoderTest, TestEchoLaunched) {
  TEST_DECODE_MESSAGE(Echo, EchoString,
                      "request test.fidlcat.examples/Echo.EchoString = {\n"
                      "  value: string = \"Hello World\"\n"
                      "}\n",
                      "Hello World", [](::fidl::StringPtr) {});
}

TEST_F(MessageDecoderTest, TestEchoAttached) {
  process_koid_ = 0x1234;
  TEST_DECODE_MESSAGE(Echo, EchoString,
                      "Can't determine request/response. it can be:\n"
                      "  request test.fidlcat.examples/Echo.EchoString = {\n"
                      "    value: string = \"Hello World\"\n"
                      "  }\n"
                      "  response test.fidlcat.examples/Echo.EchoString = {\n"
                      "    response: string = \"Hello World\"\n"
                      "  }\n",
                      "Hello World", [](::fidl::StringPtr) {});
}

}  // namespace fidlcat
