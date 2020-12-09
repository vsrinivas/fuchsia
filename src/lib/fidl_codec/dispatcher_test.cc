// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/test/frobinator_impl.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <test/fidlcodec/examples/cpp/fidl.h>

#include "src/lib/fidl_codec/encoder.h"
#include "src/lib/fidl_codec/fidl_codec_test.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/library_loader_test_data.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_parser.h"

namespace fidl_codec {

using test::fidlcodec::examples::FidlCodecTestInterface;

LibraryLoader* GetLoader();
std::array<std::string, 2> TwoStringArrayFromVals(const std::string& v1, const std::string& v2);

const uint64_t kProcessKoid = 123456;
const uint32_t kHandle = 0x6789;

class DispatcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    display_options_ = {
        .pretty_print = true, .with_process_info = false, .columns = 80, .needs_colors = false};
    dispatcher_ = std::make_unique<MessageDecoderDispatcher>(GetLoader(), display_options_);
  }

  MessageDecoderDispatcher* dispatcher() const { return dispatcher_.get(); }

 private:
  DisplayOptions display_options_;
  std::unique_ptr<MessageDecoderDispatcher> dispatcher_;
};

TEST_F(DispatcherTest, TwoStringArrayInt) {
  fidl::IncomingMessageBuffer buffer;
  fidl::HLCPPIncomingMessage message = buffer.CreateEmptyIncomingMessage();
  InterceptRequest<FidlCodecTestInterface>(
      message, [&](fidl::InterfacePtr<FidlCodecTestInterface>& ptr) {
        ptr->TwoStringArrayInt(TwoStringArrayFromVals("harpo", "chico"), 1);
      });

  DecodedMessage decoded_message;
  std::stringstream error_stream;
  decoded_message.DecodeMessage(dispatcher(), kProcessKoid, kHandle, message.bytes().data(),
                                message.bytes().size(), nullptr, 0, SyscallFidlType::kOutputMessage,
                                error_stream);
  auto result = std::make_unique<fidl_codec::FidlMessageValue>(&decoded_message, error_stream.str(),
                                                               message.bytes().data(),
                                                               message.bytes().size(), nullptr, 0);

  std::stringstream output;
  PrettyPrinter printer(output, dispatcher()->colors(), /*pretty_print=*/true, /*line_header=*/"",
                        /*max_line_size=*/dispatcher()->columns(),
                        /*header_on_every_line=*/false);
  result->PrettyPrint(nullptr, printer);
  ASSERT_EQ(output.str(),
            "sent request test.fidlcodec.examples/FidlCodecTestInterface.TwoStringArrayInt = {\n"
            "  arr: array<string> = [ \"harpo\", \"chico\" ]\n"
            "  i32: int32 = 1\n"
            "}\n");
}

TEST_F(DispatcherTest, TwoStringArrayIntIncorrect) {
  fidl::IncomingMessageBuffer buffer;
  fidl::HLCPPIncomingMessage message = buffer.CreateEmptyIncomingMessage();
  InterceptRequest<FidlCodecTestInterface>(
      message, [&](fidl::InterfacePtr<FidlCodecTestInterface>& ptr) {
        ptr->TwoStringArrayInt(TwoStringArrayFromVals("harpo", "chico"), 1);
      });

  DecodedMessage decoded_message;
  std::stringstream error_stream;
  decoded_message.DecodeMessage(dispatcher(), kProcessKoid, kHandle, message.bytes().data(),
                                message.bytes().size() - 1, nullptr, 0,
                                SyscallFidlType::kOutputMessage, error_stream);
  auto result = std::make_unique<fidl_codec::FidlMessageValue>(&decoded_message, error_stream.str(),
                                                               message.bytes().data(),
                                                               message.bytes().size(), nullptr, 0);

  std::stringstream output;
  PrettyPrinter printer(output, dispatcher()->colors(), /*pretty_print=*/true, /*line_header=*/"",
                        /*max_line_size=*/100,
                        /*header_on_every_line=*/false);
  result->PrettyPrint(nullptr, printer);
  ASSERT_EQ(output.str(),
            "sent request errors:\n"
            "  40: Not enough data to decode (needs 8, remains 7)\n"
            "sent request test.fidlcodec.examples/FidlCodecTestInterface.TwoStringArrayInt = {\n"
            "  arr: array<string> = [ \"harpo\", \"chico\" ]\n"
            "  i32: int32 = 1\n"
            "}\n");
}

}  // namespace fidl_codec
