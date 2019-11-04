// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/test/test_util.h>
#include <lib/fidl/transformer.h>
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

TEST(Conformance, UnionMigration_SingleVariant_v1_Decode) {
  auto input = std::vector<uint8_t>{

      // Header:
      0x00, 0x00, 0x00, 0x00, // TXID
      0x01, 0x00, 0x00, // Flags
      0x01, // Magic number
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Ordinal
      // Body:
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  auto handles = std::vector<zx_handle_t>{};

  fidl::test::unionmigration::SingleVariantUnionStructWithHeader obj;
  fidl::test::unionmigration::SingleVariantUnionStruct v1;
  fidl::test::unionmigration::SingleVariantUnion v2;
  uint32_t v3 = 42ull;
  v2.set_x(std::move(v3));
  v1.u = std::move(v2);
  obj.body = std::move(v1);

  Message message(BytePart(input.data(), input.size(), input.size()), HandlePart());

  const char* err;
  zx_status_t status = message.Decode(
      fidl::test::unionmigration::SingleVariantUnionStructWithHeader::FidlType, &err);
  EXPECT_EQ(status, ZX_OK);

  fidl::Decoder decoder(std::move(message));
  fidl::test::unionmigration::SingleVariantUnionStructWithHeader output;
  fidl::test::unionmigration::SingleVariantUnionStructWithHeader::Decode(&decoder, &output, 0);

  EXPECT_TRUE(::fidl::Equals(output.body, v1));
}

}  // namespace
}  // namespace fidl
