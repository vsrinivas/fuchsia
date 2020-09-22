// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/wire_parser.h"

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
#include "src/lib/fidl_codec/logger.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "src/lib/fidl_codec/wire_object.h"

namespace fidl_codec {

constexpr uint32_t kUninitialized = 0xdeadbeef;
constexpr float kFloatValue = 0.25;
constexpr double kDoubleValue = 9007199254740992.0;
constexpr int kUint32Precision = 8;

const Colors FakeColors(/*new_reset=*/"#rst#", /*new_red=*/"#red#", /*new_green=*/"#gre#",
                        /*new_blue=*/"#blu#", /*new_white_on_magenta=*/"#wom#",
                        /*new_yellow_background=*/"#yeb#");

AsyncLoopForTest::AsyncLoopForTest() : impl_(std::make_unique<AsyncLoopForTestImpl>()) {}

AsyncLoopForTest::~AsyncLoopForTest() = default;

zx_status_t AsyncLoopForTest::RunUntilIdle() { return impl_->loop()->RunUntilIdle(); }

zx_status_t AsyncLoopForTest::Run() { return impl_->loop()->Run(); }

async_dispatcher_t* AsyncLoopForTest::dispatcher() { return impl_->loop()->dispatcher(); }

LibraryLoader* InitLoader() {
  LibraryReadError err;
  auto loader = new LibraryLoader();
  fidl_codec_test::FidlcodecExamples examples;
  for (const auto& element : examples.map()) {
    loader->AddContent(element.second, &err);
  }
  return loader;
}

LibraryLoader* GetLoader() {
  static LibraryLoader* loader = InitLoader();
  return loader;
}

class WireParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loader_ = GetLoader();
    ASSERT_NE(loader_, nullptr);
  }

  LibraryLoader* loader() const { return loader_; }

 private:
  LibraryLoader* loader_;
};

TEST_F(WireParserTest, ParseSingleString) {
  fidl::MessageBuffer buffer;
  fidl::Message message = buffer.CreateEmptyMessage();

  InterceptRequest<fidl::test::frobinator::Frobinator>(
      message, [](fidl::InterfacePtr<fidl::test::frobinator::Frobinator>& ptr) {
        ptr->Grob("one", [](const fidl::StringPtr& /*value*/) { FAIL(); });
      });

  fidl_message_header_t header = message.header();

  const std::vector<const InterfaceMethod*>* methods = loader()->GetByOrdinal(header.ordinal);
  ASSERT_NE(methods, nullptr);
  ASSERT_TRUE(!methods->empty());
  const InterfaceMethod* method = (*methods)[0];
  ASSERT_NE(method, nullptr);
  ASSERT_EQ("Grob", method->name());

  zx_handle_info_t* handle_infos = nullptr;
  if (message.handles().size() > 0) {
    handle_infos = new zx_handle_info_t[message.handles().size()];
    for (uint32_t i = 0; i < message.handles().size(); ++i) {
      handle_infos[i].handle = message.handles().data()[i];
      handle_infos[i].type = ZX_OBJ_TYPE_NONE;
      handle_infos[i].rights = 0;
    }
  }

  std::unique_ptr<fidl_codec::StructValue> decoded_request;
  std::stringstream error_stream;
  fidl_codec::DecodeRequest(method, message.bytes().data(), message.bytes().size(), handle_infos,
                            message.handles().size(), &decoded_request, error_stream);
  rapidjson::Document actual;
  if (decoded_request != nullptr) {
    decoded_request->ExtractJson(actual.GetAllocator(), actual);
  }

  rapidjson::Document expected;
  expected.Parse(R"JSON({"value":"one"})JSON");
  ASSERT_EQ(expected, actual);

  delete[] handle_infos;
}

// This is a general-purpose macro for calling InterceptRequest and checking its
// results.  It can be generalized to a wide variety of types (and is, below).
// It checks for successful parsing, as well as failure when parsing truncated
// values.
// |_iface| is the interface method name on examples::FidlCodecTestInterface
//    (TODO: generalize which interface to use)
// |_json_value| is the expected JSON representation of the message.
// |_pretty_print| is the expected pretty print of the message.
// The remaining parameters are the parameters to |_iface| to generate the
// message.
// If patched_offset is not -1, we patch the encoded buffer with patched_value.
// This is useful when we want to test that we can decode junk data.
// If num_bytes is not -1, instead of decoding the full buffer, we only decode num_bytes of buffer.
// This is helpful when we want to test display of incorect data.
#define TEST_DECODE_WIRE_BODY_COMMON(_iface, patched_offset, patched_value, _json_value,           \
                                     _pretty_print, num_bytes, ...)                                \
  do {                                                                                             \
    fidl::MessageBuffer buffer;                                                                    \
    fidl::Message message = buffer.CreateEmptyMessage();                                           \
    using test::fidlcodec::examples::FidlCodecTestInterface;                                       \
    InterceptRequest<FidlCodecTestInterface>(                                                      \
        message,                                                                                   \
        [&](fidl::InterfacePtr<FidlCodecTestInterface>& ptr) { ptr->_iface(__VA_ARGS__); });       \
                                                                                                   \
    fidl_message_header_t header = message.header();                                               \
                                                                                                   \
    const std::vector<const InterfaceMethod*>* methods = loader()->GetByOrdinal(header.ordinal);   \
    ASSERT_NE(methods, nullptr);                                                                   \
    ASSERT_TRUE(!methods->empty());                                                                \
    const InterfaceMethod* method = (*methods)[0];                                                 \
    ASSERT_NE(method, nullptr);                                                                    \
    ASSERT_EQ(#_iface, method->name());                                                            \
                                                                                                   \
    zx_handle_info_t* handle_infos = nullptr;                                                      \
    if (message.handles().size() > 0) {                                                            \
      handle_infos = new zx_handle_info_t[message.handles().size()];                               \
      for (uint32_t i = 0; i < message.handles().size(); ++i) {                                    \
        handle_infos[i].handle = message.handles().data()[i];                                      \
        handle_infos[i].type = ZX_OBJ_TYPE_CHANNEL;                                                \
        handle_infos[i].rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE |              \
                                 ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER | ZX_RIGHT_WAIT |          \
                                 ZX_RIGHT_INSPECT;                                                 \
      }                                                                                            \
    }                                                                                              \
    if (patched_offset != -1) {                                                                    \
      *(reinterpret_cast<uint64_t*>(message.bytes().data() + patched_offset)) = patched_value;     \
    }                                                                                              \
                                                                                                   \
    std::stringstream error_stream;                                                                \
    MessageDecoder decoder(message.bytes().data(),                                                 \
                           (num_bytes == -1) ? message.bytes().size() : num_bytes, handle_infos,   \
                           message.handles().size(), error_stream);                                \
    std::unique_ptr<StructValue> object = decoder.DecodeMessage(*method->request());               \
    if ((num_bytes == -1) && (patched_offset == -1)) {                                             \
      std::cerr << error_stream.str();                                                             \
      ASSERT_FALSE(decoder.HasError()) << "Could not decode message";                              \
    }                                                                                              \
    rapidjson::Document actual;                                                                    \
    if (object != nullptr) {                                                                       \
      object->ExtractJson(actual.GetAllocator(), actual);                                          \
    }                                                                                              \
    rapidjson::StringBuffer actual_string;                                                         \
    rapidjson::Writer<rapidjson::StringBuffer> actual_w(actual_string);                            \
    actual.Accept(actual_w);                                                                       \
                                                                                                   \
    rapidjson::Document expected;                                                                  \
    std::string expected_source = _json_value;                                                     \
    expected.Parse(expected_source.c_str());                                                       \
    rapidjson::StringBuffer expected_string;                                                       \
    rapidjson::Writer<rapidjson::StringBuffer> expected_w(expected_string);                        \
    expected.Accept(expected_w);                                                                   \
                                                                                                   \
    ASSERT_EQ(expected, actual) << "expected = " << expected_string.GetString() << " ("            \
                                << expected_source << ")"                                          \
                                << " and actual = " << actual_string.GetString();                  \
                                                                                                   \
    std::stringstream result;                                                                      \
    if (object != nullptr) {                                                                       \
      PrettyPrinter printer(result, FakeColors, false, "", 80, /*header_on_every_line=*/false);    \
      object->PrettyPrint(nullptr, printer);                                                       \
    }                                                                                              \
    ASSERT_EQ(result.str(), _pretty_print)                                                         \
        << "expected = " << _pretty_print << " actual = " << result.str();                         \
                                                                                                   \
    for (uint32_t actual = 0; actual < message.bytes().actual(); ++actual) {                       \
      std::stringstream error_stream;                                                              \
      MessageDecoder decoder(message.bytes().data(), actual, handle_infos,                         \
                             message.handles().size(), error_stream);                              \
      std::unique_ptr<StructValue> object = decoder.DecodeMessage(*method->request());             \
      ASSERT_TRUE(decoder.HasError()) << "expect decoder error for buffer size " << actual         \
                                      << " instead of " << message.bytes().actual();               \
    }                                                                                              \
                                                                                                   \
    for (uint32_t actual = 0; message.handles().actual() > actual; actual++) {                     \
      std::stringstream error_stream;                                                              \
      MessageDecoder decoder(message.bytes().data(), message.bytes().size(), handle_infos, actual, \
                             error_stream);                                                        \
      std::unique_ptr<StructValue> object = decoder.DecodeMessage(*method->request());             \
      ASSERT_TRUE(decoder.HasError()) << "expect decoder error for handle size " << actual         \
                                      << " instead of " << message.handles().actual();             \
    }                                                                                              \
                                                                                                   \
    if ((num_bytes == -1) && (patched_offset == -1)) {                                             \
      auto encode_result = Encoder::EncodeMessage(header.txid, header.ordinal, header.flags,       \
                                                  header.magic_number, *object.get());             \
      ASSERT_THAT(encode_result.bytes, ::testing::ElementsAreArray(message.bytes()));              \
      ASSERT_EQ(message.handles().size(), encode_result.handles.size());                           \
                                                                                                   \
      for (uint32_t i = 0; i < message.handles().size(); ++i) {                                    \
        EXPECT_EQ(message.handles().data()[i], encode_result.handles[i].handle);                   \
      }                                                                                            \
    }                                                                                              \
                                                                                                   \
    delete[] handle_infos;                                                                         \
  } while (0)

#define TEST_DECODE_WIRE_BODY(_iface, _json_value, _pretty_print, ...) \
  TEST_DECODE_WIRE_BODY_COMMON(_iface, -1, 0, _json_value, _pretty_print, -1, __VA_ARGS__)

#define TEST_DECODE_WIRE_BODY_BAD(_iface, _json_value, _pretty_print, num_bytes, ...) \
  TEST_DECODE_WIRE_BODY_COMMON(_iface, -1, 0, _json_value, _pretty_print, num_bytes, __VA_ARGS__)

// This is a convenience wrapper for calling TEST_DECODE_WIRE_BODY that simply
// executes the code in a test.
// |_testname| is the name of the test (prepended by Parse in the output)
// |_iface| is the interface method name on examples::FidlCodecTestInterface
//    (TODO: generalize which interface to use)
// |_json_value| is the expected JSON representation of the message.
// |_pretty_print| is the expected pretty print of the message.
// The remaining parameters are the parameters to |_iface| to generate the
// message.
#define TEST_DECODE_WIRE(_testname, _iface, _json_value, _pretty_print, ...) \
  TEST_F(WireParserTest, Parse##_testname) {                                 \
    TEST_DECODE_WIRE_BODY(_iface, _json_value, _pretty_print, __VA_ARGS__);  \
  }

#define TEST_DECODE_WIRE_PATCHED(_testname, _iface, patched_offset, patched_value, _json_value, \
                                 _pretty_print, ...)                                            \
  TEST_F(WireParserTest, Parse##_testname) {                                                    \
    TEST_DECODE_WIRE_BODY_COMMON(_iface, patched_offset, patched_value, _json_value,            \
                                 _pretty_print, -1, __VA_ARGS__);                               \
  }

// Scalar Tests

namespace {

template <class T>
std::string ValueToJson(const std::string& key, T value) {
  return "\"" + key + "\":\"" + std::to_string(value) + "\"";
}

template <>
std::string ValueToJson(const std::string& key, bool value) {
  return "\"" + key + "\":\"" + (value ? "true" : "false") + "\"";
}

template <>
std::string ValueToJson(const std::string& key, const char* value) {
  return "\"" + key + "\":\"" + value + "\"";
}

template <>
std::string ValueToJson(const std::string& key, std::string value) {
  return "\"" + key + "\":\"" + value + "\"";
}

template <class T>
std::string SingleToJson(const std::string& key, T value) {
  return "{ " + ValueToJson(key, value) + " }";
}

template <class T>
std::string ValueToPretty(const std::string& key, const std::string& type, T value) {
  return key + ": #gre#" + type + "#rst# = #blu#" + std::to_string(value) + "#rst#";
}

template <>
std::string ValueToPretty(const std::string& key, const std::string& type, bool value) {
  return key + ": #gre#" + type + "#rst# = #blu#" + (value ? "true" : "false") + "#rst#";
}

template <>
std::string ValueToPretty(const std::string& key, const std::string& type, const char* value) {
  return key + ": #gre#" + type + "#rst# = #red#\"" + value + "\"#rst#";
}

template <>
std::string ValueToPretty(const std::string& key, const std::string& type, std::string value) {
  return key + ": #gre#" + type + "#rst# = #red#\"" + value + "\"#rst#";
}

std::string HandleToJson(const std::string& key, zx_handle_t value) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(kUint32Precision) << value << std::dec
     << std::setw(0);
  return "\"" + key + "\":\"Channel:" + ss.str() +
         "(ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | " +
         "ZX_RIGHT_SIGNAL_PEER | ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT)\"";
}

std::string HandleToPretty(const std::string& key, zx_handle_t value) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(kUint32Precision) << value << std::dec
     << std::setw(0);
  return key + ": #gre#handle#rst# = #red#Channel:" + ss.str() + "#rst#(#blu#" +
         "ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | " +
         "ZX_RIGHT_SIGNAL_PEER | ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT" + "#rst#)";
}

template <class T>
std::string SingleToPretty(const std::string& key, const std::string& type, T value) {
  return "{ " + ValueToPretty(key, type, value) + " }";
}

}  // namespace

#define TEST_SINGLE(_testname, _iface, _key, _type, _value)        \
  TEST_DECODE_WIRE(_testname, _iface, SingleToJson(#_key, _value), \
                   SingleToPretty(#_key, #_type, _value), _value)

TEST_DECODE_WIRE(Empty, Empty, "{}", "{}")

TEST_SINGLE(String, String, s, string, "Hello World!")

TEST_DECODE_WIRE_PATCHED(StringBadSize, String, 16, 100, "{\"s\":\"(invalid)\"}",
                         "{ s: #gre#string#rst# = #red#invalid#rst# }", "Hello World!")

TEST_DECODE_WIRE_PATCHED(StringHugeSize, String, 16, ULLONG_MAX, "{\"s\":\"(invalid)\"}",
                         "{ s: #gre#string#rst# = #red#invalid#rst# }", "Hello World!")

TEST_SINGLE(BoolTrue, Bool, b, bool, true)
TEST_SINGLE(BoolFalse, Bool, b, bool, false)

TEST_SINGLE(Int8Min, Int8, i8, int8, std::numeric_limits<int8_t>::min())
TEST_SINGLE(Int16Min, Int16, i16, int16, std::numeric_limits<int16_t>::min())
TEST_SINGLE(Int32Min, Int32, i32, int32, std::numeric_limits<int32_t>::min())
TEST_SINGLE(Int64Min, Int64, i64, int64, std::numeric_limits<int64_t>::min())
TEST_SINGLE(Int8Max, Int8, i8, int8, std::numeric_limits<int8_t>::max())
TEST_SINGLE(Int16Max, Int16, i16, int16, std::numeric_limits<int16_t>::max())
TEST_SINGLE(Int32Max, Int32, i32, int32, std::numeric_limits<int32_t>::max())
TEST_SINGLE(Int64Max, Int64, i64, int64, std::numeric_limits<int64_t>::max())

TEST_SINGLE(Uint8Min, Uint8, ui8, uint8, std::numeric_limits<uint8_t>::min())
TEST_SINGLE(Uint16Min, Uint16, ui16, uint16, std::numeric_limits<uint16_t>::min())
TEST_SINGLE(Uint32Min, Uint32, ui32, uint32, std::numeric_limits<uint32_t>::min())
TEST_SINGLE(Uint64Min, Uint64, ui64, uint64, std::numeric_limits<uint64_t>::min())
TEST_SINGLE(Uint8Max, Uint8, ui8, uint8, std::numeric_limits<uint8_t>::max())
TEST_SINGLE(Uint16Max, Uint16, ui16, uint16, std::numeric_limits<uint16_t>::max())
TEST_SINGLE(Uint32Max, Uint32, ui32, uint32, std::numeric_limits<uint32_t>::max())
TEST_SINGLE(Uint64Max, Uint64, ui64, uint64, std::numeric_limits<uint64_t>::max())

TEST_SINGLE(Float32, Float32, f32, float32, kFloatValue)
TEST_SINGLE(Float64, Float64, f64, float64, kDoubleValue)

TEST_DECODE_WIRE(TwoTuple, Complex, R"({"real":"1", "imaginary":"2"})",
                 "{ " + ValueToPretty("real", "int32", 1) + ", " +
                     ValueToPretty("imaginary", "int32", 2) + " }",
                 1, 2);

TEST_DECODE_WIRE(StringInt, StringInt, R"({"s":"groucho", "i32":"4"})",
                 "{ " + ValueToPretty("s", "string", "groucho") + ", " +
                     ValueToPretty("i32", "int32", 4) + " }",
                 "groucho", 4)

// Vector / Array Tests

namespace {
std::array<int32_t, 1> one_param_array = {1};
std::array<int32_t, 2> two_param_array = {1, 2};

std::vector<int32_t> one_param_vector = {1};
std::vector<int32_t> two_param_vector = {1, 2};

}  // namespace

TEST_DECODE_WIRE(Array1, Array1, R"({"b_1":["1"]})",
                 "{ b_1: array<#gre#int32#rst#> = [ #blu#1#rst# ] }", one_param_array);

TEST_DECODE_WIRE(Array2, Array2, R"({"b_2":["1", "2"]})",
                 "{ b_2: array<#gre#int32#rst#> = [ #blu#1#rst#, #blu#2#rst# ] }", two_param_array);

TEST_DECODE_WIRE(NullVector, Vector, R"({"v_1": null})",
                 "{ v_1: vector<#gre#int32#rst#> = #red#null#rst# }", nullptr)

TEST_DECODE_WIRE(VectorOneElt, Vector, R"({"v_1":["1"]})",
                 "{ v_1: vector<#gre#int32#rst#> = [ #blu#1#rst# ] }", one_param_vector);

TEST_DECODE_WIRE(VectorTwoElt, Vector, R"({"v_1":["1", "2"]})",
                 "{ v_1: vector<#gre#int32#rst#> = [ #blu#1#rst#, #blu#2#rst# ] }",
                 two_param_vector);

std::array<std::string, 2> TwoStringArrayFromVals(const std::string& v1, const std::string& v2) {
  std::array<std::string, 2> brother_array;
  brother_array[0] = v1;
  brother_array[1] = v2;
  return brother_array;
}

TEST_DECODE_WIRE(TwoStringArrayInt, TwoStringArrayInt, R"({"arr":["harpo","chico"], "i32":"1"})",
                 R"({ arr: array<#gre#string#rst#> = [ #red#"harpo"#rst#, #red#"chico"#rst# ], )" +
                     ValueToPretty("i32", "int32", 1) + " }",
                 TwoStringArrayFromVals("harpo", "chico"), 1)

namespace {

std::vector<std::string> TwoStringVectorFromVals(const std::string& v1, const std::string& v2) {
  return std::vector<std::string>({v1, v2});
}

std::vector<uint8_t> VectorUint8() {
  const int kItemCount = 40;
  std::vector<std::uint8_t> result;
  for (int i = 0; i <= kItemCount; ++i) {
    result.push_back(i);
  }
  return result;
}

std::vector<uint8_t> VectorUint8(const char* text) {
  std::vector<std::uint8_t> result;
  while (*text != 0) {
    result.push_back(*text++);
  }
  return result;
}

std::vector<uint32_t> VectorUint32() {
  const int kItemCount = 25;
  std::vector<std::uint32_t> result;
  for (int i = 0; i <= kItemCount; ++i) {
    const int kShift = 16;
    result.push_back(i + ((i & 1) << kShift));
  }
  return result;
}

}  // namespace

TEST_DECODE_WIRE(TwoStringVectorInt, TwoStringVectorInt, R"({"vec":["harpo", "chico"], "i32":"1"})",
                 R"({ vec: vector<#gre#string#rst#> = [ #red#"harpo"#rst#, #red#"chico"#rst# ], )" +
                     ValueToPretty("i32", "int32", 1) + " }",
                 TwoStringVectorFromVals("harpo", "chico"), 1)

TEST_DECODE_WIRE(TwoStringVectors, TwoStringVectors,
                 R"({"v_1":["harpo","chico"],"v_2":["groucho","zeppo"]})",
                 "{\n  v_1: vector<#gre#string#rst#> = "
                 R"([ #red#"harpo"#rst#, #red#"chico"#rst# ])"
                 "\n  v_2: vector<#gre#string#rst#> = "
                 R"([ #red#"groucho"#rst#, #red#"zeppo"#rst# ])"
                 "\n}",
                 TwoStringVectorFromVals("harpo", "chico"),
                 TwoStringVectorFromVals("groucho", "zeppo"))

TEST_DECODE_WIRE(
    VectorUint8, VectorUint8,
    R"({"v":["0","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32","33","34","35","36","37","38","39","40"]})",
    "{\n"
    "  v: vector<#gre#uint8#rst#> = [\n"
    "    #blu#0#rst#, #blu#1#rst#, #blu#2#rst#, #blu#3#rst#, #blu#4#rst#, #blu#5#rst#, "
    "#blu#6#rst#, #blu#7#rst#, #blu#8#rst#, #blu#9#rst#, #blu#10#rst#, #blu#11#rst#, #blu#12#rst#, "
    "#blu#13#rst#, #blu#14#rst#, #blu#15#rst#, #blu#16#rst#, #blu#17#rst#, #blu#18#rst#, "
    "#blu#19#rst#, #blu#20#rst#\n"
    "    #blu#21#rst#, #blu#22#rst#, #blu#23#rst#, #blu#24#rst#, #blu#25#rst#, #blu#26#rst#, "
    "#blu#27#rst#, #blu#28#rst#, #blu#29#rst#, #blu#30#rst#, #blu#31#rst#, #blu#32#rst#, "
    "#blu#33#rst#, #blu#34#rst#, #blu#35#rst#, #blu#36#rst#, #blu#37#rst#, #blu#38#rst#, "
    "#blu#39#rst#\n"
    "    #blu#40#rst#\n"
    "  ]\n"
    "}",
    VectorUint8())

TEST_DECODE_WIRE(
    VectorUint8String, VectorUint8,
    R"({"v":["72","101","108","108","111","32","116","101","115","116","105","110","103","32","119","111","114","108","100","33"]})",
    "{ v: vector<#gre#uint8#rst#> = #red#\"Hello testing world!\"#rst# }",
    VectorUint8("Hello testing world!"))

TEST_DECODE_WIRE(
    VectorUint8MultilineString, VectorUint8,
    R"({"v":["72","101","108","108","111","32","116","101","115","116","105","110","103","32","119","111","114","108","100","33","10","72","111","119","32","97","114","101","32","121","111","117","32","116","111","100","97","121", "63","10","73","39","109","32","116","101","115","116","105","110","103","32","102","105","100","108","95","99","111","100","101","99","46"]})",
    "{\n"
    "  v: vector<#gre#uint8#rst#> = [\n"
    "    #red#Hello testing world!\n"
    "    How are you today?\n"
    "    I'm testing fidl_codec.#rst#\n"
    "  ]\n"
    "}",
    VectorUint8("Hello testing world!\nHow are you today?\nI'm testing fidl_codec."))

TEST_DECODE_WIRE(
    VectorUint32, VectorUint32,
    R"({"v":["0","65537","2","65539","4","65541","6","65543","8","65545","10","65547","12","65549","14","65551","16","65553","18","65555","20","65557","22","65559","24","65561"]})",
    "{\n"
    "  v: vector<#gre#uint32#rst#> = [\n"
    "    #blu#0#rst#, #blu#65537#rst#, #blu#2#rst#, #blu#65539#rst#, #blu#4#rst#, #blu#65541#rst#, "
    "#blu#6#rst#, #blu#65543#rst#, #blu#8#rst#, #blu#65545#rst#, #blu#10#rst#, #blu#65547#rst#, "
    "#blu#12#rst#, #blu#65549#rst#, #blu#14#rst#\n"
    "    #blu#65551#rst#, #blu#16#rst#, #blu#65553#rst#, #blu#18#rst#, #blu#65555#rst#, "
    "#blu#20#rst#, #blu#65557#rst#, #blu#22#rst#, #blu#65559#rst#, #blu#24#rst#, #blu#65561#rst#\n"
    "  ]\n"
    "}",
    VectorUint32())

TEST_DECODE_WIRE_PATCHED(
    VectorUint32BadSize, VectorUint32, 16, 100000,
    R"({"v":["0","65537","2","65539","4","65541","6","65543","8","65545","10","65547","12","65549","14","65551","16","65553","18","65555","20","65557","22","65559","24","65561"]})",
    "{\n"
    "  v: vector<#gre#uint32#rst#> = [\n"
    "    #blu#0#rst#, #blu#65537#rst#, #blu#2#rst#, #blu#65539#rst#, #blu#4#rst#, #blu#65541#rst#, "
    "#blu#6#rst#, #blu#65543#rst#, #blu#8#rst#, #blu#65545#rst#, #blu#10#rst#, #blu#65547#rst#, "
    "#blu#12#rst#, #blu#65549#rst#, #blu#14#rst#\n"
    "    #blu#65551#rst#, #blu#16#rst#, #blu#65553#rst#, #blu#18#rst#, #blu#65555#rst#, "
    "#blu#20#rst#, #blu#65557#rst#, #blu#22#rst#, #blu#65559#rst#, #blu#24#rst#, #blu#65561#rst#\n"
    "  ]\n"
    "}",
    VectorUint32())

// Struct Tests

namespace {

class StructSupport {
 public:
  StructSupport() {
    pt.s = "Hello";
    pt.b = true;
    pt.i8 = std::numeric_limits<int8_t>::min();
    pt.i16 = std::numeric_limits<int16_t>::min();
    pt.i32 = std::numeric_limits<int32_t>::min();
    pt.i64 = std::numeric_limits<int64_t>::min();
    pt.u8 = std::numeric_limits<uint8_t>::max();
    pt.u16 = std::numeric_limits<uint16_t>::max();
    pt.u32 = std::numeric_limits<uint32_t>::max();
    pt.u64 = std::numeric_limits<uint64_t>::max();
    pt.f32 = kFloatValue;
    pt.f64 = kDoubleValue;
  }

  std::string GetJson() {
    std::ostringstream es;
    es << R"({"p":{)" << ValueToJson("s", pt.s) << "," << ValueToJson("b", pt.b) << ","
       << ValueToJson("i8", pt.i8) << "," << ValueToJson("i16", pt.i16) << ","
       << ValueToJson("i32", pt.i32) << "," << ValueToJson("i64", pt.i64) << ","
       << ValueToJson("u8", pt.u8) << "," << ValueToJson("u16", pt.u16) << ","
       << ValueToJson("u32", pt.u32) << "," << ValueToJson("u64", pt.u64) << ","
       << ValueToJson("f32", pt.f32) << "," << ValueToJson("f64", pt.f64) << "}}";
    return es.str();
  }
  std::string GetPretty() {
    std::ostringstream es;
    es << "{\n"
       << "  p: #gre#test.fidlcodec.examples/PrimitiveTypes#rst# = {\n"
       << "    " << ValueToPretty("s", "string", pt.s) << "\n"
       << "    " << ValueToPretty("b", "bool", pt.b) << "\n"
       << "    " << ValueToPretty("i8", "int8", pt.i8) << "\n"
       << "    " << ValueToPretty("i16", "int16", pt.i16) << "\n"
       << "    " << ValueToPretty("i32", "int32", pt.i32) << "\n"
       << "    " << ValueToPretty("i64", "int64", pt.i64) << "\n"
       << "    " << ValueToPretty("u8", "uint8", pt.u8) << "\n"
       << "    " << ValueToPretty("u16", "uint16", pt.u16) << "\n"
       << "    " << ValueToPretty("u32", "uint32", pt.u32) << "\n"
       << "    " << ValueToPretty("u64", "uint64", pt.u64) << "\n"
       << "    " << ValueToPretty("f32", "float32", pt.f32) << "\n"
       << "    " << ValueToPretty("f64", "float64", pt.f64) << "\n"
       << "  }\n"
       << "}";
    return es.str();
  }

  test::fidlcodec::examples::PrimitiveTypes pt;
};

}  // namespace

TEST_F(WireParserTest, ParseStruct) {
  StructSupport sd;
  TEST_DECODE_WIRE_BODY(Struct, sd.GetJson(), sd.GetPretty(), sd.pt);
}

TEST_F(WireParserTest, BadBoolStruct) {
  test::fidlcodec::examples::BoolStructType s;
  TEST_DECODE_WIRE_BODY_BAD(BoolStruct, "{\"s\":{\"b\":\"(invalid)\"}}",
                            "{ s: #gre#test.fidlcodec.examples/BoolStructType#rst# = "
                            "{ b: #gre#bool#rst# = #red#invalid#rst# } }",
                            16, s);
}

TEST_DECODE_WIRE(NullableStruct, NullableStruct, R"({"p":null})",
                 "{ p: #gre#test.fidlcodec.examples/PrimitiveTypes#rst# = #red#null#rst# }",
                 nullptr);

TEST_DECODE_WIRE(NullableStructAndInt, NullableStructAndInt, R"({"p":null, "i":"1"})",
                 "{ p: #gre#test.fidlcodec.examples/PrimitiveTypes#rst# = "
                 "#red#null#rst#, i: #gre#int32#rst# = #blu#1#rst# }",
                 nullptr, 1);

namespace {

std::array<std::unique_ptr<test::fidlcodec::examples::TwoStringStruct>, 3> GetArrayNullableStruct(
    const std::string& v1, const std::string& v2, const std::string& v3, const std::string& v4) {
  std::array<std::unique_ptr<test::fidlcodec::examples::TwoStringStruct>, 3> a;
  a[0] = std::make_unique<test::fidlcodec::examples::TwoStringStruct>();
  a[0]->value1 = v1;
  a[0]->value2 = v2;
  a[1] = nullptr;
  a[2] = std::make_unique<test::fidlcodec::examples::TwoStringStruct>();
  a[2]->value1 = v3;
  a[2]->value2 = v4;
  return a;
}

}  // namespace

TEST_DECODE_WIRE(
    ArrayNullableStruct, ArrayNullableStruct,
    R"({"a":[{"value1":"harpo","value2":"chico"},null,{"value1":"groucho","value2":"zeppo"}]})",
    "{\n"
    "  a: array<#gre#test.fidlcodec.examples/TwoStringStruct#rst#> = [\n"
    "    { value1: #gre#string#rst# = #red#\"harpo\"#rst#, "
    "value2: #gre#string#rst# = #red#\"chico\"#rst# }, #red#null#rst#\n"
    "    { value1: #gre#string#rst# = #red#\"groucho\"#rst#, "
    "value2: #gre#string#rst# = #red#\"zeppo\"#rst# }\n"
    "  ]\n"
    "}",
    GetArrayNullableStruct("harpo", "chico", "groucho", "zeppo"))

namespace {

test::fidlcodec::examples::SmallStruct SmallStructFromVals(uint8_t a, uint8_t b, uint8_t c) {
  test::fidlcodec::examples::SmallStruct ss;
  ss.a = a;
  ss.b = b;
  ss.c = c;
  return ss;
}

}  // namespace

TEST_DECODE_WIRE(SmallStruct, SmallStructAfterByte,
                 R"({"u":"1","s1":{"a":"2","b":"3","c":"4"},"s2":{"a":"5","b":"6","c":"7"}})",
                 "{\n"
                 "  u: #gre#uint8#rst# = #blu#1#rst#\n"
                 "  s1: #gre#test.fidlcodec.examples/SmallStruct#rst# = {\n"
                 "    a: #gre#uint8#rst# = #blu#2#rst#\n"
                 "    b: #gre#uint8#rst# = #blu#3#rst#\n"
                 "    c: #gre#uint8#rst# = #blu#4#rst#\n"
                 "  }\n"
                 "  s2: #gre#test.fidlcodec.examples/SmallStruct#rst# = {\n"
                 "    a: #gre#uint8#rst# = #blu#5#rst#\n"
                 "    b: #gre#uint8#rst# = #blu#6#rst#\n"
                 "    c: #gre#uint8#rst# = #blu#7#rst#\n"
                 "  }\n"
                 "}",
                 1, SmallStructFromVals(2, 3, 4), SmallStructFromVals(5, 6, 7));

namespace {

test::fidlcodec::examples::TwoStringStruct TwoStringStructFromVals(const std::string& v1,
                                                                   const std::string& v2) {
  test::fidlcodec::examples::TwoStringStruct tss;
  tss.value1 = v1;
  tss.value2 = v2;
  return tss;
}

std::unique_ptr<test::fidlcodec::examples::TwoStringStruct> TwoStringStructFromValsPtr(
    const std::string& v1, const std::string& v2) {
  std::unique_ptr<test::fidlcodec::examples::TwoStringStruct> ptr(
      new test::fidlcodec::examples::TwoStringStruct());
  ptr->value1 = v1;
  ptr->value2 = v2;
  return ptr;
}

std::string TwoStringStructIntPretty(const char* s1, const char* s2, int v) {
  std::string result = "{\n  s: #gre#test.fidlcodec.examples/TwoStringStruct#rst# = {\n";
  result += "    " + ValueToPretty("value1", "string", s1) + "\n";
  result += "    " + ValueToPretty("value2", "string", s2) + "\n";
  result += "  }\n";
  result += "  " + ValueToPretty("i32", "int32", v) + "\n";
  result += "}";
  return result;
}

}  // namespace

TEST_DECODE_WIRE(TwoStringStructInt, TwoStringStructInt,
                 R"({"s":{"value1":"harpo", "value2":"chico"}, "i32":"1"})",
                 TwoStringStructIntPretty("harpo", "chico", 1),
                 TwoStringStructFromVals("harpo", "chico"), 1)

TEST_DECODE_WIRE(TwoStringNullableStructInt, TwoStringNullableStructInt,
                 R"({"s":{"value1":"harpo", "value2":"chico"}, "i32":"1"})",
                 TwoStringStructIntPretty("harpo", "chico", 1),
                 TwoStringStructFromValsPtr("harpo", "chico"), 1)

TEST_DECODE_WIRE(VectorStruct, VectorStruct,
                 R"({"v":[{"a":"1","b":"2","c":"3"},{"a":"2","b":"4","c":"6"},)"
                 R"({"a":"3","b":"6","c":"9"}]})",
                 "{\n"
                 "  v: vector<#gre#test.fidlcodec.examples/SmallStruct#rst#> = [\n"
                 "    { a: #gre#uint8#rst# = #blu#1#rst#, b: #gre#uint8#rst# = #blu#2#rst#, c: "
                 "#gre#uint8#rst# = #blu#3#rst# }\n"
                 "    { a: #gre#uint8#rst# = #blu#2#rst#, b: #gre#uint8#rst# = #blu#4#rst#, c: "
                 "#gre#uint8#rst# = #blu#6#rst# }\n"
                 "    { a: #gre#uint8#rst# = #blu#3#rst#, b: #gre#uint8#rst# = #blu#6#rst#, c: "
                 "#gre#uint8#rst# = #blu#9#rst# }\n"
                 "  ]\n"
                 "}",
                 std::vector{SmallStructFromVals(1, 2, 3), SmallStructFromVals(2, 4, 6),
                             SmallStructFromVals(3, 6, 9)})

TEST_DECODE_WIRE(ArrayStruct, ArrayStruct,
                 R"({"a":[{"a":"1","b":"2","c":"3"},{"a":"2","b":"4","c":"6"},)"
                 R"({"a":"3","b":"6","c":"9"}]})",
                 "{\n"
                 "  a: array<#gre#test.fidlcodec.examples/SmallStruct#rst#> = [\n"
                 "    { a: #gre#uint8#rst# = #blu#1#rst#, b: #gre#uint8#rst# = #blu#2#rst#, c: "
                 "#gre#uint8#rst# = #blu#3#rst# }\n"
                 "    { a: #gre#uint8#rst# = #blu#2#rst#, b: #gre#uint8#rst# = #blu#4#rst#, c: "
                 "#gre#uint8#rst# = #blu#6#rst# }\n"
                 "    { a: #gre#uint8#rst# = #blu#3#rst#, b: #gre#uint8#rst# = #blu#6#rst#, c: "
                 "#gre#uint8#rst# = #blu#9#rst# }\n"
                 "  ]\n"
                 "}",
                 std::array{SmallStructFromVals(1, 2, 3), SmallStructFromVals(2, 4, 6),
                            SmallStructFromVals(3, 6, 9)})

namespace {

test::fidlcodec::examples::SmallUnevenStruct SmallUnevenStructFromVals(uint8_t a, uint8_t b) {
  test::fidlcodec::examples::SmallUnevenStruct ss;
  ss.a = a;
  ss.b = b;
  return ss;
}

}  // namespace

TEST_DECODE_WIRE(VectorStruct2, VectorStruct2,
                 R"({"v":[{"a":"1","b":"2"},{"a":"2","b":"4"},{"a":"3","b":"6"}]})",
                 "{\n"
                 "  v: vector<#gre#test.fidlcodec.examples/SmallUnevenStruct#rst#> = [\n"
                 "    { a: #gre#uint32#rst# = #blu#1#rst#, b: #gre#uint8#rst# = #blu#2#rst# },"
                 " { a: #gre#uint32#rst# = #blu#2#rst#, b: #gre#uint8#rst# = #blu#4#rst# }\n"
                 "    { a: #gre#uint32#rst# = #blu#3#rst#, b: #gre#uint8#rst# = #blu#6#rst# }\n"
                 "  ]\n"
                 "}",
                 std::vector{SmallUnevenStructFromVals(1, 2), SmallUnevenStructFromVals(2, 4),
                             SmallUnevenStructFromVals(3, 6)})

TEST_DECODE_WIRE(ArrayStruct2, ArrayStruct2,
                 R"({"a":[{"a":"1","b":"2"},{"a":"2","b":"4"},{"a":"3","b":"6"}]})",
                 "{\n"
                 "  a: array<#gre#test.fidlcodec.examples/SmallUnevenStruct#rst#> = [\n"
                 "    { a: #gre#uint32#rst# = #blu#1#rst#, b: #gre#uint8#rst# = #blu#2#rst# },"
                 " { a: #gre#uint32#rst# = #blu#2#rst#, b: #gre#uint8#rst# = #blu#4#rst# }\n"
                 "    { a: #gre#uint32#rst# = #blu#3#rst#, b: #gre#uint8#rst# = #blu#6#rst# }\n"
                 "  ]\n"
                 "}",
                 std::array{SmallUnevenStructFromVals(1, 2), SmallUnevenStructFromVals(2, 4),
                            SmallUnevenStructFromVals(3, 6)})

// Union and XUnion tests

namespace {

using isu = test::fidlcodec::examples::IntStructUnion;
using xisu = test::fidlcodec::examples::IntStructXunion;
using u8u16struct = test::fidlcodec::examples::U8U16UnionStructType;

template <class T>
T GetIntUnion(int32_t i) {
  T u;
  u.set_variant_i(i);
  return u;
}

template <class T>
T GetStructUnion(const std::string& v1, const std::string& v2) {
  T u;
  test::fidlcodec::examples::TwoStringStruct tss = TwoStringStructFromVals(v1, v2);
  u.set_variant_tss(tss);
  return u;
}

template <class T>
std::unique_ptr<T> GetIntUnionPtr(int32_t i) {
  std::unique_ptr<T> ptr(new T());
  ptr->set_variant_i(i);
  return ptr;
}

template <class T>
std::unique_ptr<T> GetStructUnionPtr(const std::string& v1, const std::string& v2) {
  std::unique_ptr<T> ptr(new T());
  test::fidlcodec::examples::TwoStringStruct tss = TwoStringStructFromVals(v1, v2);
  ptr->set_variant_tss(tss);
  return ptr;
}

u8u16struct GetU8U16UnionStruct(int8_t i) {
  u8u16struct s;
  s.u.set_variant_u8(i);
  return s;
}

std::string IntUnionIntPretty(const std::string& name, int u, int v) {
  std::string result = "{\n";
  result += "  isu: #gre#test.fidlcodec.examples/" + name + "#rst# = { " +
            ValueToPretty("variant_i", "int32", u) + " }\n";
  result += "  " + ValueToPretty("i", "int32", v) + "\n";
  result += "}";
  return result;
}

std::string StructUnionIntPretty(const std::string& name, const char* u1, const char* u2, int v) {
  std::string result = "{\n";
  result += "  isu: #gre#test.fidlcodec.examples/" + name + "#rst# = {\n";
  result +=
      "    variant_tss: #gre#test.fidlcodec.examples/TwoStringStruct#rst# = "
      "{\n";
  result += "      " + ValueToPretty("value1", "string", u1) + "\n";
  result += "      " + ValueToPretty("value2", "string", u2) + "\n";
  result += "    }\n";
  result += "  }\n";
  result += "  " + ValueToPretty("i", "int32", v) + "\n";
  result += "}";
  return result;
}

std::string IntIntUnionPretty(const std::string& name, int v, int u) {
  std::string result = "{\n";
  result += "  " + ValueToPretty("i", "int32", v) + "\n";
  result += "  isu: #gre#test.fidlcodec.examples/" + name + "#rst# = { " +
            ValueToPretty("variant_i", "int32", u) + " }\n";
  result += "}";
  return result;
}

std::string IntStructUnionPretty(const std::string& name, int v, const char* u1, const char* u2) {
  std::string result = "{\n";
  result += "  " + ValueToPretty("i", "int32", v) + "\n";
  result += "  isu: #gre#test.fidlcodec.examples/" + name + "#rst# = {\n";
  result +=
      "    variant_tss: #gre#test.fidlcodec.examples/TwoStringStruct#rst# = "
      "{\n";
  result += "      " + ValueToPretty("value1", "string", u1) + "\n";
  result += "      " + ValueToPretty("value2", "string", u2) + "\n";
  result += "    }\n";
  result += "  }\n";
  result += "}";
  return result;
}

test::fidlcodec::examples::DataElement GetDataElement(int32_t i32, uint8_t u8) {
  test::fidlcodec::examples::DataElement result;
  std::vector<std::unique_ptr<test::fidlcodec::examples::DataElement>> alternatives;
  auto item_1 = std::make_unique<test::fidlcodec::examples::DataElement>();
  item_1->set_int32(i32);
  alternatives.emplace_back(std::move(item_1));
  auto item_2 = std::make_unique<test::fidlcodec::examples::DataElement>();
  item_2->set_uint8(u8);
  alternatives.emplace_back(std::move(item_2));
  result.set_alternatives(std::move(alternatives));
  return result;
}

}  // namespace

TEST_DECODE_WIRE(UnionInt, Union, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                 IntUnionIntPretty("IntStructUnion", 42, 1), GetIntUnion<isu>(42), 1);

TEST_DECODE_WIRE(UnionStruct, Union,
                 R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
                 StructUnionIntPretty("IntStructUnion", "harpo", "chico", 1),
                 GetStructUnion<isu>("harpo", "chico"), 1);

TEST_DECODE_WIRE(NullableUnionInt, NullableUnion, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                 IntUnionIntPretty("IntStructUnion", 42, 1), GetIntUnionPtr<isu>(42), 1);

TEST_DECODE_WIRE(NullableUnionStruct, NullableUnion,
                 R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
                 StructUnionIntPretty("IntStructUnion", "harpo", "chico", 1),
                 GetStructUnionPtr<isu>("harpo", "chico"), 1);

TEST_DECODE_WIRE(NullableUnionIntFirstInt, NullableUnionIntFirst,
                 R"({"i" : "1", "isu":{"variant_i":"42"}})",
                 IntIntUnionPretty("IntStructUnion", 1, 42), 1, GetIntUnionPtr<isu>(42));

TEST_DECODE_WIRE(NullableUnionIntFirstStruct, NullableUnionIntFirst,
                 R"({"i": "1", "isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}})",
                 IntStructUnionPretty("IntStructUnion", 1, "harpo", "chico"), 1,
                 GetStructUnionPtr<isu>("harpo", "chico"));

TEST_DECODE_WIRE(XUnionInt, XUnion, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                 IntUnionIntPretty("IntStructXunion", 42, 1), GetIntUnion<xisu>(42), 1);

TEST_DECODE_WIRE(XUnionStruct, XUnion,
                 R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
                 StructUnionIntPretty("IntStructXunion", "harpo", "chico", 1),
                 GetStructUnion<xisu>("harpo", "chico"), 1);

TEST_DECODE_WIRE(NullableXUnionInt, NullableXUnion, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                 IntUnionIntPretty("IntStructXunion", 42, 1), GetIntUnionPtr<xisu>(42), 1);

TEST_DECODE_WIRE(NullableXUnionStruct, NullableXUnion,
                 R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
                 StructUnionIntPretty("IntStructXunion", "harpo", "chico", 1),
                 GetStructUnionPtr<xisu>("harpo", "chico"), 1);

TEST_DECODE_WIRE(NullableXUnionIntFirstInt, NullableXUnionIntFirst,
                 R"({"i" : "1", "isu":{"variant_i":"42"}})",
                 IntIntUnionPretty("IntStructXunion", 1, 42), 1, GetIntUnionPtr<xisu>(42));

TEST_DECODE_WIRE(NullableXUnionIntFirstStruct, NullableXUnionIntFirst,
                 R"({"i": "1", "isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}})",
                 IntStructUnionPretty("IntStructXunion", 1, "harpo", "chico"), 1,
                 GetStructUnionPtr<xisu>("harpo", "chico"));

TEST_DECODE_WIRE(
    RecursiveUnion, RecursiveUnion, R"({"e":{"alternatives":[{"int32":"-10"},{"uint8":"200"}]}})",
    "{\n"
    "  e: #gre#test.fidlcodec.examples/DataElement#rst# = {\n"
    "    alternatives: vector<#gre#test.fidlcodec.examples/DataElement#rst#> = [\n"
    "      { int32: #gre#int32#rst# = #blu#-10#rst# }, { uint8: #gre#uint8#rst# = #blu#200#rst# }\n"
    "    ]\n"
    "  }\n"
    "}",
    GetDataElement(-10, 200));

namespace {

std::array<std::unique_ptr<test::fidlcodec::examples::IntStructUnion>, 3> GetArrayNullableUnion(
    int32_t i, const std::string& v1, const std::string& v2) {
  std::array<std::unique_ptr<test::fidlcodec::examples::IntStructUnion>, 3> a;
  a[0] = std::make_unique<test::fidlcodec::examples::IntStructUnion>();
  a[0]->set_variant_i(i);
  test::fidlcodec::examples::TwoStringStruct tss = TwoStringStructFromVals(v1, v2);
  a[2] = std::make_unique<test::fidlcodec::examples::IntStructUnion>();
  a[2]->set_variant_tss(tss);
  return a;
}

}  // namespace

TEST_DECODE_WIRE(
    ArrayNullableUnion, ArrayNullableUnion,
    R"({"a":[{"variant_i":"1234"},null,{"variant_tss":{"value1":"harpo","value2":"chico"}}]})",
    "{\n"
    "  a: array<#gre#test.fidlcodec.examples/IntStructUnion#rst#> = [\n"
    "    { variant_i: #gre#int32#rst# = #blu#1234#rst# }, #red#null#rst#\n"
    "    {\n"
    "      variant_tss: #gre#test.fidlcodec.examples/TwoStringStruct#rst# = {\n"
    "        value1: #gre#string#rst# = #red#\"harpo\"#rst#\n"
    "        value2: #gre#string#rst# = #red#\"chico\"#rst#\n"
    "      }\n"
    "    }\n"
    "  ]\n"
    "}",
    GetArrayNullableUnion(1234, "harpo", "chico"))

TEST_F(WireParserTest, BadU8U16UnionStruct) {
  TEST_DECODE_WIRE_BODY_COMMON(U8U16UnionStruct, -1, 0,
                               "{\"s\":{\"u\":{\"variant_u8\":\"(invalid)\"}}}",
                               "{\n"
                               "  s: #gre#test.fidlcodec.examples/U8U16UnionStructType#rst# = {\n"
                               "    u: #gre#test.fidlcodec.examples/U8U16Union#rst# = "
                               "{ variant_u8: #gre#uint8#rst# = #red#invalid#rst# }\n"
                               "  }\n"
                               "}",
                               24, GetU8U16UnionStruct(12));
}

namespace {

using uuu = test::fidlcodec::examples::U8U16Union;
using uuur = test::fidlcodec::examples::U8U16UnionReserved;
using uux = test::fidlcodec::examples::U8U16Xunion;

template <class T>
T GetUInt8Union(uint8_t i) {
  T u;
  u.set_variant_u8(i);
  return u;
}

template <class T>
T GetUInt16Union(uint16_t i) {
  T u;
  u.set_variant_u16(i);
  return u;
}

std::string ShortUnionPretty(const std::string& name, const char* field, const char* type, int u,
                             int v) {
  std::string result = "{\n";
  result += "  u: #gre#test.fidlcodec.examples/" + name + "#rst# = { " +
            ValueToPretty(field, type, u) + " }\n";
  result += "  " + ValueToPretty("i", "int32", v) + "\n";
  result += "}";
  return result;
}

}  // namespace

TEST_DECODE_WIRE(ShortUnion8, ShortUnion, R"({"u":{"variant_u8":"16"}, "i":"1"})",
                 ShortUnionPretty("U8U16Union", "variant_u8", "uint8", 16, 1),
                 GetUInt8Union<uuu>(16), 1);

TEST_DECODE_WIRE(ShortUnion16, ShortUnion, R"({"u":{"variant_u16":"1024"}, "i":"1"})",
                 ShortUnionPretty("U8U16Union", "variant_u16", "uint16", 1024, 1),
                 GetUInt16Union<uuu>(1024), 1);

TEST_DECODE_WIRE(ShortUnionReserved8, ShortUnionReserved, R"({"u":{"variant_u8":"16"}, "i":"1"})",
                 ShortUnionPretty("U8U16UnionReserved", "variant_u8", "uint8", 16, 1),
                 GetUInt8Union<uuur>(16), 1);

TEST_DECODE_WIRE(ShortUnionReserved16, ShortUnionReserved,
                 R"({"u":{"variant_u16":"1024"}, "i":"1"})",
                 ShortUnionPretty("U8U16UnionReserved", "variant_u16", "uint16", 1024, 1),
                 GetUInt16Union<uuur>(1024), 1);

TEST_DECODE_WIRE(ShortXUnion8, ShortXUnion, R"({"u":{"variant_u8":"16"}, "i":"1"})",
                 ShortUnionPretty("U8U16Xunion", "variant_u8", "uint8", 16, 1),
                 GetUInt8Union<uux>(16), 1);

TEST_DECODE_WIRE(ShortXUnion16, ShortXUnion, R"({"u":{"variant_u16":"1024"}, "i":"1"})",
                 ShortUnionPretty("U8U16Xunion", "variant_u16", "uint16", 1024, 1),
                 GetUInt16Union<uux>(1024), 1);

// Enum Tests

TEST_DECODE_WIRE(DefaultEnum, DefaultEnumMessage, R"({"ev":"X"})",
                 "{ ev: #gre#test.fidlcodec.examples/DefaultEnum#rst# = #blu#X#rst# }",
                 test::fidlcodec::examples::DefaultEnum::X);
TEST_DECODE_WIRE(I8Enum, I8EnumMessage, R"({"ev":"X"})",
                 "{ ev: #gre#test.fidlcodec.examples/I8Enum#rst# = #blu#X#rst# }",
                 test::fidlcodec::examples::I8Enum::X);
TEST_DECODE_WIRE(I16Enum, I16EnumMessage, R"({"ev":"X"})",
                 "{ ev: #gre#test.fidlcodec.examples/I16Enum#rst# = #blu#X#rst# }",
                 test::fidlcodec::examples::I16Enum::X);
TEST_DECODE_WIRE(I32Enum, I32EnumMessage, R"({"ev":"X"})",
                 "{ ev: #gre#test.fidlcodec.examples/I32Enum#rst# = #blu#X#rst# }",
                 test::fidlcodec::examples::I32Enum::X);
TEST_DECODE_WIRE(I64Enum, I64EnumMessage, R"({"ev":"X"})",
                 "{ ev: #gre#test.fidlcodec.examples/I64Enum#rst# = #blu#X#rst# }",
                 test::fidlcodec::examples::I64Enum::X);

// Bits Tests

TEST_DECODE_WIRE(DefaultBits, DefaultBitsMessage, R"({"v":"A|C"})",
                 "{ v: #gre#test.fidlcodec.examples/DefaultBits#rst# = #blu#A|C#rst# }",
                 static_cast<test::fidlcodec::examples::DefaultBits>(
                     static_cast<uint8_t>(test::fidlcodec::examples::DefaultBits::A) |
                     static_cast<uint8_t>(test::fidlcodec::examples::DefaultBits::C)));
TEST_DECODE_WIRE(I8Bits, I8BitsMessage, R"({"v":"A|D"})",
                 "{ v: #gre#test.fidlcodec.examples/I8Bits#rst# = #blu#A|D#rst# }",
                 static_cast<test::fidlcodec::examples::I8Bits>(
                     static_cast<uint8_t>(test::fidlcodec::examples::I8Bits::A) |
                     static_cast<uint8_t>(test::fidlcodec::examples::I8Bits::D)));
TEST_DECODE_WIRE(I16Bits, I16BitsMessage, R"({"v":"B|C"})",
                 "{ v: #gre#test.fidlcodec.examples/I16Bits#rst# = #blu#B|C#rst# }",
                 static_cast<test::fidlcodec::examples::I16Bits>(
                     static_cast<uint16_t>(test::fidlcodec::examples::I16Bits::B) |
                     static_cast<uint16_t>(test::fidlcodec::examples::I16Bits::C)));
TEST_DECODE_WIRE(I32Bits, I32BitsMessage, R"({"v":"B|D"})",
                 "{ v: #gre#test.fidlcodec.examples/I32Bits#rst# = #blu#B|D#rst# }",
                 static_cast<test::fidlcodec::examples::I32Bits>(
                     static_cast<uint32_t>(test::fidlcodec::examples::I32Bits::B) |
                     static_cast<uint32_t>(test::fidlcodec::examples::I32Bits::D)));
TEST_DECODE_WIRE(I64Bits, I64BitsMessage, R"({"v":"C|D"})",
                 "{ v: #gre#test.fidlcodec.examples/I64Bits#rst# = #blu#C|D#rst# }",
                 static_cast<test::fidlcodec::examples::I64Bits>(
                     static_cast<uint64_t>(test::fidlcodec::examples::I64Bits::C) |
                     static_cast<uint64_t>(test::fidlcodec::examples::I64Bits::D)));
TEST_DECODE_WIRE(EmptyDefaultBits, DefaultBitsMessage, R"({"v":"<none>"})",
                 "{ v: #gre#test.fidlcodec.examples/DefaultBits#rst# = #blu#<none>#rst# }",
                 static_cast<test::fidlcodec::examples::DefaultBits>(0));

// Table Tests

test::fidlcodec::examples::ValueTable GetTable(std::optional<int16_t> first_int16,
                                               std::optional<std::string> value1,
                                               std::optional<std::string> value2,
                                               std::optional<int32_t> third_union_val) {
  test::fidlcodec::examples::ValueTable table;
  if (first_int16.has_value()) {
    table.set_first_int16(*first_int16);
  }
  if (value1.has_value()) {
    table.set_second_struct(TwoStringStructFromVals(*value1, *value2));
  }
  if (third_union_val.has_value()) {
    test::fidlcodec::examples::IntStructUnion u;
    u.set_variant_i(*third_union_val);
    table.set_third_union(std::move(u));
  }
  return table;
}

std::string TablePretty(std::optional<int16_t> first_int16, std::optional<std::string> value1,
                        std::optional<std::string> value2, std::optional<int32_t> third_union_val,
                        int i) {
  if (!first_int16.has_value() && !value1.has_value() && !third_union_val.has_value()) {
    std::string result = "{ table: #gre#test.fidlcodec.examples/ValueTable#rst# = {}, ";
    result += ValueToPretty("i", "int32", i);
    result += " }";
    return result;
  }
  std::string result = "{\n";
  if (!value1.has_value() && !third_union_val.has_value()) {
    result += "  table: #gre#test.fidlcodec.examples/ValueTable#rst# = { ";
    result += ValueToPretty("first_int16", "int16", *first_int16) + " }\n";
  } else {
    result += "  table: #gre#test.fidlcodec.examples/ValueTable#rst# = {\n";
    if (first_int16.has_value()) {
      result += "    " + ValueToPretty("first_int16", "int16", *first_int16) + "\n";
    }
    if (value1.has_value()) {
      result +=
          "    second_struct: "
          "#gre#test.fidlcodec.examples/TwoStringStruct#rst# = {\n";
      result += "      " + ValueToPretty("value1", "string", *value1) + "\n";
      result += "      " + ValueToPretty("value2", "string", *value2) + "\n";
      result += "    }\n";
    }
    if (third_union_val.has_value()) {
      result += "    third_union: #gre#test.fidlcodec.examples/IntStructUnion#rst# = {\n";
      result += "      " + ValueToPretty("variant_i", "int32", *third_union_val) + "\n";
      result += "    }\n";
    }
    result += "  }\n";
  }
  result += "  " + ValueToPretty("i", "int32", i) + "\n";
  result += "}";
  return result;
}

TEST_DECODE_WIRE(Table0, Table, R"({"table":{}, "i":"2"})", TablePretty({}, {}, {}, {}, 2),
                 GetTable({}, {}, {}, {}), 2)

TEST_DECODE_WIRE(Table1, Table,
                 R"({"table":{
                          "third_union":{"variant_i":"42"}
                      },
                      "i":"2"})",
                 TablePretty({}, {}, {}, 42, 2), GetTable({}, {}, {}, 42), 2)

TEST_DECODE_WIRE(Table2, Table,
                 R"({"table":{
                          "second_struct":{"value1":"harpo", "value2":"groucho"}
                      },
                      "i":"2"})",
                 TablePretty({}, "harpo", "groucho", {}, 2), GetTable({}, "harpo", "groucho", {}),
                 2)

TEST_DECODE_WIRE(Table3, Table,
                 R"({"table":{
                          "second_struct":{
                              "value1":"harpo", "value2":"groucho"},
                          "third_union":{"variant_i":"42"}},
                      "i":"2"})",
                 TablePretty({}, "harpo", "groucho", 42, 2), GetTable({}, "harpo", "groucho", 42),
                 2)

TEST_DECODE_WIRE(Table4, Table, R"({"table":{
                                         "first_int16":"1"
                                     },
                                     "i":"2"})",
                 TablePretty(1, {}, {}, {}, 2), GetTable(1, {}, {}, {}), 2)

TEST_DECODE_WIRE(Table5, Table,
                 R"({"table":{
                          "first_int16":"1",
                          "third_union":{"variant_i":"42"}
                      },
                      "i":"2"})",
                 TablePretty(1, {}, {}, 42, 2), GetTable(1, {}, {}, 42), 2)

TEST_DECODE_WIRE(Table6, Table,
                 R"({"table":{
                          "first_int16":"1",
                          "second_struct":{
                              "value1":"harpo", "value2":"groucho"}
                      },
                      "i":"2"})",
                 TablePretty(1, "harpo", "groucho", {}, 2), GetTable(1, "harpo", "groucho", {}), 2)

TEST_DECODE_WIRE(Table7, Table,
                 R"({"table":{
                          "first_int16":"1",
                          "second_struct":{
                              "value1":"harpo", "value2":"groucho"},
                          "third_union":{"variant_i":"42"}
                      },
                      "i":"2"})",
                 TablePretty(1, "harpo", "groucho", 42, 2), GetTable(1, "harpo", "groucho", 42), 2)

// TODO(fxbug.dev/6274): Add a test that exercises what happens when we encounter an
// unknown type in a table.

// Handle Tests

namespace {

class HandleSupport {
 public:
  HandleSupport() {
    zx::channel::create(0, &out1_, &out2_);
    json_ = "{" + HandleToJson("ch", out2_.get()) + "}";
    pretty_ = "{ " + HandleToPretty("ch", out2_.get()) + " }";
  }
  zx::channel handle() { return std::move(out2_); }

  template <typename Interface>
  fidl::InterfaceHandle<Interface> interface() {
    return fidl::InterfaceHandle<Interface>(std::move(out2_));
  }

  std::string GetJSON() { return json_; }
  std::string GetPretty() { return pretty_; }

 private:
  zx::channel out1_;
  zx::channel out2_;
  std::string json_;
  std::string pretty_;
};

}  // namespace

TEST_F(WireParserTest, ParseHandle) {
  HandleSupport support;
  TEST_DECODE_WIRE_BODY(Handle, support.GetJSON(), support.GetPretty(), support.handle());
}
TEST_F(WireParserTest, ParseNullableHandle) {
  HandleSupport support;
  TEST_DECODE_WIRE_BODY(NullableHandle, support.GetJSON(), support.GetPretty(), support.handle());
}
TEST_F(WireParserTest, ParseProtocol) {
  HandleSupport support;
  TEST_DECODE_WIRE_BODY(Protocol, support.GetJSON(), support.GetPretty(),
                        support.interface<test::fidlcodec::examples::ParamProtocol>());
}

namespace {

class HandleStructSupport {
 public:
  HandleStructSupport() {
    zx::channel::create(0, &out1_, &out2_);
    zx::channel::create(0, &out3_, &out4_);
    json_ = "{\"hs\":{" + HandleToJson("h1", out1_.get()) + "," + HandleToJson("h2", out2_.get()) +
            "," + HandleToJson("h3", out3_.get()) + "}}";
    pretty_ = "{\n  hs: #gre#test.fidlcodec.examples/HandleStruct#rst# = {\n    " +
              HandleToPretty("h1", out1_.get()) + "\n    " + HandleToPretty("h2", out2_.get()) +
              "\n    " + HandleToPretty("h3", out3_.get()) + "\n  }\n}";
  }
  test::fidlcodec::examples::HandleStruct handle_struct() {
    test::fidlcodec::examples::HandleStruct hs;
    hs.h1 = std::move(out1_);
    hs.h2 = std::move(out2_);
    hs.h3 = std::move(out3_);
    return hs;
  }
  std::string GetJSON() { return json_; }
  std::string GetPretty() { return pretty_; }

 private:
  zx::channel out1_;
  zx::channel out2_;
  zx::channel out3_;
  zx::channel out4_;

  std::string json_;
  std::string pretty_;
};

}  // namespace

TEST_F(WireParserTest, ParseHandleStruct) {
  HandleStructSupport support;
  TEST_DECODE_WIRE_BODY(HandleStructMessage, support.GetJSON(), support.GetPretty(),
                        support.handle_struct());
}

namespace {

class HandleTableSupport {
 public:
  HandleTableSupport() {
    zx::channel::create(0, &out1_, &out2_);
    json_ = "{\"t\":{" + HandleToJson("h1", out1_.get()) + ",\"s1\":{\"sh1\":\"00000000\"," +
            HandleToJson("sh2", out2_.get()) + "}}}";
    pretty_ =
        "{\n"
        "  t: #gre#test.fidlcodec.examples/HandleTable#rst# = {\n"
        "    " +
        HandleToPretty("h1", out1_.get()) +
        "\n"
        "    s1: #gre#test.fidlcodec.examples/OptHandleStruct#rst# = {\n"
        "      sh1: #gre#handle#rst# = #red#00000000#rst#\n"
        "      " +
        HandleToPretty("sh2", out2_.get()) +
        "\n"
        "    }\n"
        "  }\n"
        "}";
  }
  test::fidlcodec::examples::HandleTable handle_table() {
    test::fidlcodec::examples::HandleTable t;
    t.set_h1(std::move(out1_));
    test::fidlcodec::examples::OptHandleStruct s;
    s.sh2 = std::move(out2_);
    t.set_s1(std::move(s));
    return t;
  }
  std::string GetJSON() { return json_; }
  std::string GetPretty() { return pretty_; }

 private:
  zx::channel out1_;
  zx::channel out2_;

  std::string json_;
  std::string pretty_;
};

}  // namespace

TEST_F(WireParserTest, ParseHandleTable) {
  HandleTableSupport support;
  TEST_DECODE_WIRE_BODY(HandleTableMessage, support.GetJSON(), support.GetPretty(),
                        support.handle_table());
}

namespace {

class TraversalOrderSupport {
 public:
  TraversalOrderSupport() {
    zx::channel::create(0, &sh1_, &sh2_);
    zx::channel::create(0, &h1_, &h2_);
    json_ = "{\"t\":{\"s\":{" + HandleToJson("sh1", sh1_.get()) + "," +
            HandleToJson("sh2", sh2_.get()) + "}," + HandleToJson("h1", h1_.get()) + "," +
            HandleToJson("h2", h2_.get()) + "}}";
    pretty_ =
        "{\n  t: #gre#test.fidlcodec.examples/TraversalOrder#rst# = {\n    "
        "s: #gre#test.fidlcodec.examples/OptHandleStruct#rst# = {\n      " +
        HandleToPretty("sh1", sh1_.get()) + "\n      " + HandleToPretty("sh2", sh2_.get()) +
        "\n    }\n    " + HandleToPretty("h1", h1_.get()) + "\n    " +
        HandleToPretty("h2", h2_.get()) + "\n  }\n}";
  }
  test::fidlcodec::examples::TraversalOrder TraversalOrder() {
    test::fidlcodec::examples::TraversalOrder t;
    auto s = std::make_unique<test::fidlcodec::examples::OptHandleStruct>();
    s->sh1 = std::move(sh1_);
    s->sh2 = std::move(sh2_);
    t.s = std::move(s);
    t.h1 = std::move(h1_);
    t.h2 = std::move(h2_);
    return t;
  }
  std::string GetJSON() { return json_; }
  std::string GetPretty() { return pretty_; }

 private:
  zx::channel sh1_;
  zx::channel sh2_;
  zx::channel h1_;
  zx::channel h2_;

  std::string json_;
  std::string pretty_;
};

}  // namespace

TEST_F(WireParserTest, ParseTraversalOrder) {
  TraversalOrderSupport support;
  TEST_DECODE_WIRE_BODY(TraversalOrderMessage, support.GetJSON(), support.GetPretty(),
                        support.TraversalOrder());
}

namespace {

class TraversalMainSupport {
 public:
  TraversalMainSupport() {
    zx::channel::create(0, &out1_, &out2_);
    json_ = R"JSON({"v":[{"x":"10","y":{"a":"20",)JSON" + HandleToJson("b", out1_.get()) +
            R"JSON(}},{"x":"30","y":{"a":"40",)JSON" + HandleToJson("b", out2_.get()) +
            R"JSON(}}],"s":{"a":"50","b":"00000000"}})JSON";
    pretty_ =
        "{\n"
        "  v: vector<#gre#test.fidlcodec.examples/TraversalMain#rst#> = [\n"
        "    {\n"
        "      x: #gre#uint32#rst# = #blu#10#rst#\n"
        "      y: #gre#test.fidlcodec.examples/TraversalStruct#rst# = {\n"
        "        a: #gre#uint32#rst# = #blu#20#rst#\n"
        "        " +
        HandleToPretty("b", out1_.get()) +
        "\n"
        "      }\n"
        "    }\n"
        "    {\n"
        "      x: #gre#uint32#rst# = #blu#30#rst#\n"
        "      y: #gre#test.fidlcodec.examples/TraversalStruct#rst# = {\n"
        "        a: #gre#uint32#rst# = #blu#40#rst#\n"
        "        " +
        HandleToPretty("b", out2_.get()) +
        "\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "  s: #gre#test.fidlcodec.examples/TraversalStruct#rst# = { "
        "a: #gre#uint32#rst# = #blu#50#rst#, "
        "b: #gre#handle#rst# = #red#00000000#rst# }\n"
        "}";
  }
  std::vector<std::unique_ptr<test::fidlcodec::examples::TraversalMain>> GetV() {
    std::vector<std::unique_ptr<test::fidlcodec::examples::TraversalMain>> result;
    auto object1 = std::make_unique<test::fidlcodec::examples::TraversalMain>();
    object1->x = 10;
    auto object2 = std::make_unique<test::fidlcodec::examples::TraversalStruct>();
    object2->a = 20;
    object2->b = std::move(out1_);
    object1->y = std::move(object2);
    result.emplace_back(std::move(object1));
    auto object3 = std::make_unique<test::fidlcodec::examples::TraversalMain>();
    object3->x = 30;
    auto object4 = std::make_unique<test::fidlcodec::examples::TraversalStruct>();
    object4->a = 40;
    object4->b = std::move(out2_);
    object3->y = std::move(object4);
    result.emplace_back(std::move(object3));
    return result;
  }

  std::unique_ptr<test::fidlcodec::examples::TraversalStruct> GetS() {
    auto result = std::make_unique<test::fidlcodec::examples::TraversalStruct>();
    result->a = 50;
    return result;
  }

  std::string GetJSON() { return json_; }
  std::string GetPretty() { return pretty_; }

 private:
  zx::channel out1_;
  zx::channel out2_;

  std::string json_;
  std::string pretty_;
};

}  // namespace

TEST_F(WireParserTest, ParseTraversalMain) {
  TraversalMainSupport support_v1;
  TEST_DECODE_WIRE_BODY(TraversalMainMessage, support_v1.GetJSON(), support_v1.GetPretty(),
                        support_v1.GetV(), support_v1.GetS());
}

// Corrupt data tests

TEST_F(WireParserTest, BadSchemaPrintHex) {
  std::ostringstream log_msg;
  fidl_codec::logger::LogCapturer capturer(&log_msg);
  // i32 in the schema is missing "subtype": "int32"
  std::string bad_schema = R"FIDL({
  "version": "0.0.1",
  "name": "fidl.examples.types",
  "library_dependencies": [],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "test.fidlcodec.examples/FidlCodecTestInterface",
      "location": {
        "filename": "../../src/lib/fidl_codec/testdata/types.test.fidl",
        "line": 11,
        "column": 10
      },
      "methods": [
        {
          "ordinal": 1593056155789170713,
          "name": "Int32",
          "location": {
            "filename": "../../src/lib/fidl_codec/testdata/types.test.fidl",
            "line": 16,
            "column": 5
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "primitive"
              },
              "name": "i32",
              "location": {
                "filename": "../../src/lib/fidl_codec/testdata/types.test.fidl",
                "line": 16,
                "column": 17
              },
              "size": 4,
              "max_out_of_line": 0,
              "alignment": 4,
              "offset": 16,
              "max_handles": 0,
              "field_shape_old": {
                "offset": 16,
                "padding": 0
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_request_size": 24,
          "maybe_request_alignment": 8,
          "maybe_request_type_shape_old": {
            "inline_size": 24,
            "alignment": 8,
            "depth": 0,
            "max_handles": 0,
            "max_out_of_line": 0,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "maybe_request_type_shape_v1": {
            "inline_size": 24,
            "alignment": 8,
            "depth": 0,
            "max_handles": 0,
            "max_out_of_line": 0,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "has_response": false,
          "is_composed": false
        }
      ]
    }
  ],
  "struct_declarations": [],
  "table_declarations": [],
  "union_declarations": [],
  "xunion_declarations": []
})FIDL";
  LibraryReadError err;
  LibraryLoader loader;
  loader.AddContent(bad_schema, &err);
  ASSERT_TRUE(err.value == LibraryReadError::ErrorValue::kOk);
  fidl::MessageBuffer buffer;
  fidl::Message message = buffer.CreateEmptyMessage();

  InterceptRequest<test::fidlcodec::examples::FidlCodecTestInterface>(
      message, [](fidl::InterfacePtr<test::fidlcodec::examples::FidlCodecTestInterface>& ptr) {
        ptr->Int32(kUninitialized);
      });

  fidl_message_header_t header = message.header();

  zx_handle_info_t* handle_infos = nullptr;
  if (message.handles().size() > 0) {
    handle_infos = new zx_handle_info_t[message.handles().size()];
    for (uint32_t i = 0; i < message.handles().size(); ++i) {
      handle_infos[i].handle = message.handles().data()[i];
      handle_infos[i].type = ZX_OBJ_TYPE_NONE;
      handle_infos[i].rights = 0;
    }
  }

  const std::vector<const InterfaceMethod*>* methods = loader.GetByOrdinal(header.ordinal);
  ASSERT_NE(methods, nullptr);
  ASSERT_TRUE(!methods->empty());

  const InterfaceMethod* method = (*methods)[0];
  // If this is null, you probably have to update the schema above.
  ASSERT_NE(method, nullptr);

  std::unique_ptr<fidl_codec::StructValue> decoded_request;
  std::stringstream error_stream;
  fidl_codec::DecodeRequest(method, message.bytes().data(), message.bytes().size(), handle_infos,
                            message.handles().size(), &decoded_request, error_stream);
  rapidjson::Document actual;
  if (decoded_request != nullptr) {
    decoded_request->ExtractJson(actual.GetAllocator(), actual);
  }

  // Checks that an invalid type generates an invalid value.
  ASSERT_STREQ(actual["i32"].GetString(), "(invalid)");

  delete[] handle_infos;
  ASSERT_STREQ(log_msg.str().c_str(), "Invalid type");
}

// Checks that MessageDecoder::DecodeValue doesn't core dump with a null type.
TEST_F(WireParserTest, DecodeNullTypeValue) {
  std::stringstream error_stream;
  fidl_message_header_t header;
  MessageDecoder decoder(reinterpret_cast<uint8_t*>(&header), sizeof(header), nullptr, 0,
                         error_stream);
  decoder.DecodeValue(nullptr);
}

}  // namespace fidl_codec
