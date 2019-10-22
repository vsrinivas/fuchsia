// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/test/test_util.h>
#include <zircon/types.h>

#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fidl/test/unionmigration/cpp/fidl.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/event_sender.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/internal/message_reader.h"
#include "lib/fidl/cpp/internal/proxy_controller.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

class MagicNumberMessageHandler : public internal::MessageHandler {
 public:
  zx_status_t OnMessage(Message message) override {
    is_supported = message.is_supported_version();
    return ZX_OK;
  }

  bool is_supported = false;
};

TEST(EncodeTest, EventMagicNumber) {
  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  MagicNumberMessageHandler handler;
  internal::MessageReader client(&handler);
  client.Bind(std::move(h1));
  EXPECT_TRUE(client.is_bound());

  EventSender<fidl::test::frobinator::Frobinator> sender(std::move(h2));
  EXPECT_TRUE(sender.channel().is_valid());

  auto background = std::thread([&sender]() { sender.events().Hrob("one"); });
  background.join();
  loop.RunUntilIdle();

  ASSERT_TRUE(handler.is_supported);
}

TEST(EncodeTest, RequestMagicNumber) {
  fidl::test::AsyncLoopForTest loop;
  fidl::test::frobinator::FrobinatorPtr client;

  MagicNumberMessageHandler handler;
  internal::MessageReader server(&handler);
  server.Bind(client.NewRequest().TakeChannel());
  EXPECT_TRUE(client.is_bound());
  EXPECT_TRUE(server.is_bound());

  client->Frob("one");
  loop.RunUntilIdle();

  ASSERT_TRUE(handler.is_supported);
}

// Adapted from RoundTrip in test_util.h.
TEST(Conformance, DecodeUnionFromXUnion) {
  fidl::test::unionmigration::BasicXUnionStruct input;
  input.val = fidl::test::unionmigration::BasicXUnion::WithI32(2);

  fidl::Encoder enc(0xfefefefe);
  const size_t input_encoded_size =
      EncodingInlineSize<fidl::test::unionmigration::BasicXUnionStruct, fidl::Encoder>(&enc);
  const size_t input_padding_size = FIDL_ALIGN(input_encoded_size) - input_encoded_size;
  const ::fidl::FidlStructField fake_input_interface_fields[] = {
      ::fidl::FidlStructField(fidl::test::unionmigration::BasicXUnionStruct::FidlType, 16,
                              input_padding_size),
  };
  const fidl_type_t fake_input_interface_struct{
      ::fidl::FidlCodedStruct(fake_input_interface_fields, 1, 16 + input_encoded_size, "Input")};
  const size_t output_encoded_size =
      EncodingInlineSize<fidl::test::unionmigration::BasicXUnionStruct, fidl::Encoder>(&enc);
  const size_t output_padding_size = FIDL_ALIGN(output_encoded_size) - output_encoded_size;
  const ::fidl::FidlStructField fake_output_interface_fields[] = {
      ::fidl::FidlStructField(fidl::test::unionmigration::BasicXUnionStruct::FidlType, 16,
                              output_padding_size),
  };
  const fidl_type_t fake_output_interface_struct{
      ::fidl::FidlCodedStruct(fake_output_interface_fields, 1, 16 + output_encoded_size, "Output")};

  auto ofs = enc.Alloc(input_encoded_size);
  fidl::Clone(input).Encode(&enc, ofs);
  auto msg = enc.GetMessage();

  // Set the bit indicating that this contains xunion bytes.
  fidl_message_header_t* header = reinterpret_cast<fidl_message_header_t*>(msg.bytes().data());
  header->flags[0] = 1;
  // The fidl definition of the union and xunion have different names and therefore different
  // ordinals. Update the ordinal so the union can decode from its corresponding xunion-encoded
  // bytes.
  *reinterpret_cast<uint64_t*>(msg.payload().data()) = 580704578;

  const char* err_msg = nullptr;
  EXPECT_EQ(ZX_OK, msg.Decode(&fake_output_interface_struct, &err_msg)) << err_msg;
  fidl::Decoder dec(std::move(msg));
  fidl::test::unionmigration::BasicUnionStruct output;
  fidl::test::unionmigration::BasicUnionStruct::Decode(&dec, &output, ofs);

  EXPECT_EQ(output.val.i32(), input.val.i32());
}

TEST(Conformance, EncodeUnionAsXUnion) {
  auto xunion_value = fidl::test::unionmigration::BasicXUnion::WithI32(2);
  fidl::Encoder xunion_enc(0xfefefefe);
  auto xunion_offset = xunion_enc.Alloc(
      EncodingInlineSize<fidl::test::unionmigration::BasicXUnion, fidl::Encoder>(&xunion_enc));
  fidl::Clone(xunion_value).Encode(&xunion_enc, xunion_offset);
  auto xunion_msg = xunion_enc.GetMessage();
  auto xunion_payload = xunion_msg.payload();
  // The fidl definition of the union and xunion have different names and therefore different
  // ordinals. Update the expected bytes to have tag of the union's equivalent xunion.
  *reinterpret_cast<uint64_t*>(xunion_payload.data()) = 580704578;

  fidl::test::unionmigration::BasicUnion union_value;
  union_value.set_i32(2);
  fidl::Encoder union_enc(0xfefefefe);
  union_enc.SetShouldEncodeUnionAsXUnion(true);
  auto union_offset = union_enc.Alloc(
      EncodingInlineSize<fidl::test::unionmigration::BasicUnion, fidl::Encoder>(&union_enc));
  fidl::Clone(union_value).Encode(&union_enc, union_offset);
  auto union_msg = union_enc.GetMessage();
  auto union_payload = union_msg.payload();

  EXPECT_TRUE(fidl::test::util::cmp_payload(
      reinterpret_cast<const uint8_t*>(union_payload.data()), union_payload.actual(),
      reinterpret_cast<const uint8_t*>(xunion_payload.data()), xunion_payload.size()));
}

}  // namespace
}  // namespace fidl
