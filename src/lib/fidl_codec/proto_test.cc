// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <memory>
#include <sstream>

#include <gtest/gtest.h>
#include <test/fidlcodec/examples/cpp/fidl.h>

#include "src/lib/fidl_codec/fidl_codec_test.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/printer.h"
#include "src/lib/fidl_codec/proto_value.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

class ProtoValueTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loader_ = GetLoader();
    ASSERT_NE(loader_, nullptr);
    library_ = loader()->GetLibraryFromName("test.fidlcodec.examples");
    ASSERT_NE(library_, nullptr);
    library_->DecodeAll();
  }

  LibraryLoader* loader() const { return loader_; }
  Library* library() const { return library_; }

  std::unique_ptr<FidlMessageValue> CreateFidlMessage(const fidl::Message& message) {
    std::unique_ptr<zx_handle_info_t[]> handle_infos;
    if (message.handles().size() > 0) {
      handle_infos = std::make_unique<zx_handle_info_t[]>(message.handles().size());
      for (uint32_t i = 0; i < message.handles().size(); ++i) {
        handle_infos[i].handle = message.handles().data()[i];
        handle_infos[i].type = ZX_OBJ_TYPE_NONE;
        handle_infos[i].rights = 0;
      }
    }

    DisplayOptions display_options;
    MessageDecoderDispatcher decoder(loader(), display_options);

    constexpr uint64_t kProcessKoid = 0x1234;

    DecodedMessage decoded_message;
    std::stringstream error_stream;
    decoded_message.DecodeMessage(&decoder, kProcessKoid, ZX_HANDLE_INVALID, message.bytes().data(),
                                  message.bytes().size(), nullptr, 0,
                                  SyscallFidlType::kOutputMessage, error_stream);
    return std::make_unique<fidl_codec::FidlMessageValue>(&decoded_message, error_stream.str(),
                                                          message.bytes().data(),
                                                          message.bytes().size(), nullptr, 0);
  }

 private:
  LibraryLoader* loader_ = nullptr;
  Library* library_ = nullptr;
};

class ProtoPrinter : public PrettyPrinter {
 public:
  ProtoPrinter(std::ostream& os, bool dump_messages)
      : PrettyPrinter(os, WithoutColors, true, "", 100, false), dump_messages_(dump_messages) {}

  bool DumpMessages() const override { return dump_messages_; }

 private:
  const bool dump_messages_;
};

#define PROTO_TEST(value, type, dump_messages, expected)       \
  {                                                            \
    proto::Value proto_value;                                  \
    ProtoVisitor visitor(&proto_value);                        \
    value->Visit(&visitor, nullptr);                           \
    auto new_value = DecodeValue(loader(), proto_value, type); \
    std::stringstream ss;                                      \
    ProtoPrinter printer(ss, dump_messages);                   \
    new_value->PrettyPrint(type, printer);                     \
    EXPECT_EQ(ss.str(), expected);                             \
  }

TEST_F(ProtoValueTest, NullValue) {
  PROTO_TEST(std::make_unique<NullValue>(), nullptr, false, "null");
}

TEST_F(ProtoValueTest, RawValue) {
  std::array<uint8_t, 10> data = {0, 1, 3, 5, 7, 11, 13, 17, 19, 23};
  PROTO_TEST(std::make_unique<RawValue>(data.data(), data.size()), nullptr, false,
             "00 01 03 05 07 0b 0d 11 13 17");
}

TEST_F(ProtoValueTest, BoolValue) {
  PROTO_TEST(std::make_unique<BoolValue>(false), nullptr, false, "false");
  PROTO_TEST(std::make_unique<BoolValue>(true), nullptr, false, "true");
}

TEST_F(ProtoValueTest, IntegerValue) {
  Int8Type type_int8;
  PROTO_TEST(std::make_unique<IntegerValue>(10, true), &type_int8, false, "-10");

  Int8Type type_uint32;
  PROTO_TEST(std::make_unique<IntegerValue>(123456, false), &type_uint32, false, "123456");

  std::unique_ptr<Type> enum_type =
      library()->TypeFromIdentifier(/*is_nullable=*/false, "test.fidlcodec.examples/DefaultEnum");
  ASSERT_NE(enum_type, nullptr);
  PROTO_TEST(std::make_unique<IntegerValue>(
                 static_cast<uint64_t>(test::fidlcodec::examples::DefaultEnum::X)),
             enum_type.get(), false, "X");

  std::unique_ptr<Type> bits_type =
      library()->TypeFromIdentifier(/*is_nullable=*/false, "test.fidlcodec.examples/DefaultBits");
  ASSERT_NE(bits_type, nullptr);
  PROTO_TEST(std::make_unique<IntegerValue>(5, false), bits_type.get(), false, "A|C");
}

TEST_F(ProtoValueTest, DoubleValue) {
  Float32Type type_float32;
  PROTO_TEST(std::make_unique<DoubleValue>(3.141593), &type_float32, false, "3.141593");
  Float64Type type_float64;
  PROTO_TEST(std::make_unique<DoubleValue>(3.141593), &type_float64, false, "3.141593");
}

TEST_F(ProtoValueTest, StringValue) {
  PROTO_TEST(std::make_unique<StringValue>("Hello world!"), nullptr, false, "\"Hello world!\"");
}

TEST_F(ProtoValueTest, HandleValue) {
  PROTO_TEST(std::make_unique<HandleValue>(zx_handle_disposition_t{.operation = fidl_codec::kNoHandleDisposition,
                                                                   .handle = 0x1234,
                                                                   .type = ZX_OBJ_TYPE_CHANNEL,
                                                                   .rights = ZX_RIGHT_DUPLICATE,
                                                                   .result = ZX_OK}),
             nullptr, false, "Channel:00001234(ZX_RIGHT_DUPLICATE)");
}

TEST_F(ProtoValueTest, UnionValue) {
  std::unique_ptr<Type> type = library()->TypeFromIdentifier(
      /*is_nullable=*/false, "test.fidlcodec.examples/U8U16UnionReserved");
  const UnionType* union_type = type->AsUnionType();
  ASSERT_NE(union_type, nullptr);
  UnionMember* variant_u8 = union_type->union_definition().SearchMember("variant_u8");
  ASSERT_NE(variant_u8, nullptr);
  PROTO_TEST(std::make_unique<UnionValue>(*variant_u8, std::make_unique<IntegerValue>(250, false)),
             union_type, false, "{ variant_u8: uint8 = 250 }");
  UnionMember* variant_u16 = union_type->union_definition().SearchMember("variant_u16");
  ASSERT_NE(variant_u16, nullptr);
  PROTO_TEST(
      std::make_unique<UnionValue>(*variant_u16, std::make_unique<IntegerValue>(65000, false)),
      union_type, false, "{ variant_u16: uint16 = 65000 }");
}

TEST_F(ProtoValueTest, StructValue) {
  std::unique_ptr<Type> type = library()->TypeFromIdentifier(
      /*is_nullable=*/false, "test.fidlcodec.examples/PrimitiveTypes");
  const StructType* struct_type = type->AsStructType();
  ASSERT_NE(struct_type, nullptr);
  auto value = std::make_unique<StructValue>(struct_type->struct_definition());
  value->AddField("s", std::make_unique<StringValue>("The string field"));
  value->AddField("b", std::make_unique<BoolValue>(true));
  value->AddField("i8", std::make_unique<IntegerValue>(5, false));
  value->AddField("i16", std::make_unique<IntegerValue>(5, true));
  value->AddField("i32", std::make_unique<IntegerValue>(100000, true));
  value->AddField("i64", std::make_unique<IntegerValue>(100000, false));
  value->AddField("u8", std::make_unique<IntegerValue>(250, false));
  value->AddField("u16", std::make_unique<IntegerValue>(65000, false));
  value->AddField("u32", std::make_unique<IntegerValue>(100000, false));
  value->AddField("u64", std::make_unique<IntegerValue>(100000, false));
  value->AddField("f32", std::make_unique<DoubleValue>(3.141593));
  value->AddField("f64", std::make_unique<DoubleValue>(3.141593));
  PROTO_TEST(std::move(value), struct_type, false,
             "{\n"
             "  s: string = \"The string field\"\n"
             "  b: bool = true\n"
             "  i8: int8 = 5\n"
             "  i16: int16 = -5\n"
             "  i32: int32 = -100000\n"
             "  i64: int64 = 100000\n"
             "  u8: uint8 = 250\n"
             "  u16: uint16 = 65000\n"
             "  u32: uint32 = 100000\n"
             "  u64: uint64 = 100000\n"
             "  f32: float32 = 3.141593\n"
             "  f64: float64 = 3.141593\n"
             "}");
}

TEST_F(ProtoValueTest, VectorValue) {
  std::unique_ptr<Type> type = library()->TypeFromIdentifier(
      /*is_nullable=*/false, "test.fidlcodec.examples/DataElement");
  const UnionType* union_type = type->AsUnionType();
  ASSERT_NE(union_type, nullptr);
  UnionMember* uint8 = union_type->union_definition().SearchMember("uint8");
  ASSERT_NE(uint8, nullptr);
  UnionMember* uint16 = union_type->union_definition().SearchMember("uint16");
  ASSERT_NE(uint16, nullptr);
  UnionMember* sequence = union_type->union_definition().SearchMember("sequence");
  ASSERT_NE(sequence, nullptr);
  auto vector_value = std::make_unique<VectorValue>();
  vector_value->AddValue(
      std::make_unique<UnionValue>(*uint8, std::make_unique<IntegerValue>(250, false)));
  vector_value->AddValue(std::make_unique<NullValue>());
  vector_value->AddValue(
      std::make_unique<UnionValue>(*uint16, std::make_unique<IntegerValue>(65000, false)));
  vector_value->AddValue(
      std::make_unique<UnionValue>(*uint8, std::make_unique<IntegerValue>(50, false)));
  PROTO_TEST(std::make_unique<UnionValue>(*sequence, std::move(vector_value)), union_type, false,
             "{\n"
             "  sequence: vector<test.fidlcodec.examples/DataElement> = [\n"
             "    { uint8: uint8 = 250 }, null, { uint16: uint16 = 65000 }, { uint8: uint8 = 50 }\n"
             "  ]\n"
             "}");
}

TEST_F(ProtoValueTest, TableValue) {
  std::unique_ptr<Type> type_t = library()->TypeFromIdentifier(
      /*is_nullable=*/false, "test.fidlcodec.examples/ValueTable");
  const TableType* table_type = type_t->AsTableType();
  ASSERT_NE(table_type, nullptr);

  std::unique_ptr<Type> type_u = library()->TypeFromIdentifier(
      /*is_nullable=*/false, "test.fidlcodec.examples/IntStructUnion");
  const UnionType* union_type = type_u->AsUnionType();
  ASSERT_NE(union_type, nullptr);
  UnionMember* variant_i = union_type->union_definition().SearchMember("variant_i");
  ASSERT_NE(variant_i, nullptr);

  std::unique_ptr<Type> type_s = library()->TypeFromIdentifier(
      /*is_nullable=*/false, "test.fidlcodec.examples/TwoStringStruct");
  const StructType* struct_type = type_s->AsStructType();
  ASSERT_NE(struct_type, nullptr);
  auto struct_value = std::make_unique<StructValue>(struct_type->struct_definition());
  struct_value->AddField("value1", std::make_unique<StringValue>("The value one field"));
  struct_value->AddField("value2", std::make_unique<StringValue>("The value two field"));

  auto value = std::make_unique<TableValue>(table_type->table_definition());
  value->AddMember("first_int16", std::make_unique<IntegerValue>(500, true));
  value->AddMember("second_struct", std::move(struct_value));
  value->AddMember("third_union", std::make_unique<UnionValue>(
                                      *variant_i, std::make_unique<IntegerValue>(100000, true)));

  PROTO_TEST(
      std::move(value), table_type, false,
      "{\n"
      "  first_int16: int16 = -500\n"
      "  second_struct: test.fidlcodec.examples/TwoStringStruct = {\n"
      "    value1: string = \"The value one field\"\n"
      "    value2: string = \"The value two field\"\n"
      "  }\n"
      "  third_union: test.fidlcodec.examples/IntStructUnion = { variant_i: int32 = -100000 }\n"
      "}");
}

TEST_F(ProtoValueTest, FidlMessageValue) {
  fidl::MessageBuffer buffer_;
  fidl::Message message = buffer_.CreateEmptyMessage();
  InterceptRequest<test::fidlcodec::examples::FidlCodecTestInterface>(
      message, [&](fidl::InterfacePtr<test::fidlcodec::examples::FidlCodecTestInterface>& ptr) {
        ptr->StringInt("Hello FIDL", 100);
      });
  std::unique_ptr<FidlMessageValue> fidl_message = CreateFidlMessage(message);
  PROTO_TEST(fidl_message, nullptr, false,
             "sent request test.fidlcodec.examples/FidlCodecTestInterface.StringInt = {\n"
             "  s: string = \"Hello FIDL\"\n"
             "  i32: int32 = 100\n"
             "}\n");
  PROTO_TEST(fidl_message, nullptr, true,
             "sent request test.fidlcodec.examples/FidlCodecTestInterface.StringInt = {\n"
             "  s: string = \"Hello FIDL\"\n"
             "  i32: int32 = 100\n"
             "}\n"
             "Message: num_bytes=56 num_handles=0 "
             "ordinal=432a041a7505f6aa(test.fidlcodec.examples/FidlCodecTestInterface.StringInt)\n"
             "  data=\n"
             "    0000: 00, 00, 00, 00, 00, 00, 00, 01, aa, f6, 05, 75, 1a, 04, 2a, 43, \n"
             "    0010: 0a, 00, 00, 00, 00, 00, 00, 00, ff, ff, ff, ff, ff, ff, ff, ff, \n"
             "    0020: 64, 00, 00, 00, 00, 00, 00, 00, 48, 65, 6c, 6c, 6f, 20, 46, 49, \n"
             "    0030: 44, 4c, 00, 00, 00, 00, 00, 00\n");
}

}  // namespace fidl_codec
