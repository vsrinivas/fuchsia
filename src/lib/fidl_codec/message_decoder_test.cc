// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/message_decoder.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"
#include "src/lib/fidl_codec/fidl_codec_test.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/library_loader_test_data.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "test/fidlcodec/examples/cpp/fidl.h"
#include "wire_parser.h"

using test::fidlcodec::examples::Echo;
using test::fidlcodec::examples::FidlCodecTestInterface;

namespace fidl_codec {

constexpr int kColumns = 80;
constexpr uint64_t kProcessKoid = 0x1234;

class MessageDecoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loader_ = GetLoader();
    ASSERT_NE(loader_, nullptr);
    display_options_.pretty_print = true;
    display_options_.columns = kColumns;
    decoder_ = std::make_unique<MessageDecoderDispatcher>(loader_, display_options_);
  }

  MessageDecoderDispatcher* decoder() const { return decoder_.get(); }
  uint64_t process_koid() const { return process_koid_; }
  std::stringstream& result() { return result_; }

 private:
  LibraryLoader* loader_;
  std::unique_ptr<MessageDecoderDispatcher> decoder_;
  DisplayOptions display_options_;
  uint64_t process_koid_ = kProcessKoid;
  std::stringstream result_;
};

#define TEST_DECODE_MESSAGE(_interface, _iface, _expected, ...)                              \
  do {                                                                                       \
    fidl::MessageBuffer buffer;                                                              \
    fidl::Message message = buffer.CreateEmptyMessage();                                     \
    zx_handle_t handle = ZX_HANDLE_INVALID;                                                  \
    InterceptRequest<_interface>(                                                            \
        message, [&](fidl::InterfacePtr<_interface>& ptr) { ptr->_iface(__VA_ARGS__); });    \
    zx_handle_info_t* handle_infos = nullptr;                                                \
    if (message.handles().size() > 0) {                                                      \
      handle_infos = new zx_handle_info_t[message.handles().size()];                         \
      for (uint32_t i = 0; i < message.handles().size(); ++i) {                              \
        handle_infos[i].handle = message.handles().data()[i];                                \
        handle_infos[i].type = ZX_OBJ_TYPE_NONE;                                             \
        handle_infos[i].rights = 0;                                                          \
      }                                                                                      \
    }                                                                                        \
    decoder()->DecodeMessage(process_koid(), handle, message.bytes().data(),                 \
                             message.bytes().size(), handle_infos, message.handles().size(), \
                             SyscallFidlType::kOutputMessage, result());                     \
    delete[] handle_infos;                                                                   \
    ASSERT_EQ(result().str(), _expected)                                                     \
        << "expected = " << _expected << " actual = " << result().str();                     \
  } while (0)

TEST_F(MessageDecoderTest, TestEmptyLaunched) {
  decoder()->AddLaunchedProcess(process_koid());
  TEST_DECODE_MESSAGE(FidlCodecTestInterface, Empty,
                      "sent request test.fidlcodec.examples/FidlCodecTestInterface.Empty = {}\n");
}

TEST_F(MessageDecoderTest, TestStringLaunched) {
  decoder()->AddLaunchedProcess(process_koid());
  TEST_DECODE_MESSAGE(FidlCodecTestInterface, String,
                      "sent request test.fidlcodec.examples/FidlCodecTestInterface.String = {\n"
                      "  s: string = \"Hello World\"\n"
                      "}\n",
                      "Hello World");
}

TEST_F(MessageDecoderTest, TestStringAttached) {
  TEST_DECODE_MESSAGE(FidlCodecTestInterface, String,
                      "sent request test.fidlcodec.examples/FidlCodecTestInterface.String = {\n"
                      "  s: string = \"Hello World\"\n"
                      "}\n",
                      "Hello World");
}

TEST_F(MessageDecoderTest, TestEchoLaunched) {
  decoder()->AddLaunchedProcess(process_koid());
  TEST_DECODE_MESSAGE(Echo, EchoString,
                      "sent request test.fidlcodec.examples/Echo.EchoString = {\n"
                      "  value: string = \"Hello World\"\n"
                      "}\n",
                      "Hello World", [](const ::fidl::StringPtr&) {});
}

TEST_F(MessageDecoderTest, TestEchoAttached) {
  TEST_DECODE_MESSAGE(Echo, EchoString,
                      "Can't determine request/response. it can be:\n"
                      "  sent request test.fidlcodec.examples/Echo.EchoString = {\n"
                      "    value: string = \"Hello World\"\n"
                      "  }\n"
                      "  sent response test.fidlcodec.examples/Echo.EchoString = {\n"
                      "    response: string = \"Hello World\"\n"
                      "  }\n",
                      "Hello World", [](const ::fidl::StringPtr&) {});
}

}  // namespace fidl_codec
