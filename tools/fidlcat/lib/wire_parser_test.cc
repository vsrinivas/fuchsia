// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/wire_parser.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "test/fidlcat/examples/cpp/fidl.h"
#include "tools/fidlcat/lib/fidlcat_test.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/library_loader_test_data.h"
#include "tools/fidlcat/lib/message_decoder.h"
#include "tools/fidlcat/lib/wire_object.h"

namespace fidlcat {

const Colors FakeColors(/*reset=*/"#rst#", /*red=*/"#red#", /*green=*/"#gre#",
                        /*blue=*/"#blu#", /*white_on_magenta=*/"#wom#");

AsyncLoopForTest::AsyncLoopForTest() : impl_(std::make_unique<AsyncLoopForTestImpl>()) {}

AsyncLoopForTest::~AsyncLoopForTest() = default;

zx_status_t AsyncLoopForTest::RunUntilIdle() { return impl_->loop()->RunUntilIdle(); }

zx_status_t AsyncLoopForTest::Run() { return impl_->loop()->Run(); }

async_dispatcher_t* AsyncLoopForTest::dispatcher() { return impl_->loop()->dispatcher(); }

LibraryLoader* InitLoader() {
  std::vector<std::unique_ptr<std::istream>> library_files;
  fidlcat_test::ExampleMap examples;
  for (auto element : examples.map()) {
    std::unique_ptr<std::istream> file =
        std::make_unique<std::istringstream>(std::istringstream(element.second));
    library_files.push_back(std::move(file));
  }
  LibraryReadError err;
  return new LibraryLoader(library_files, &err);
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
  };
  LibraryLoader* loader_;
};

TEST_F(WireParserTest, ParseSingleString) {
  fidl::MessageBuffer buffer;
  fidl::Message message = buffer.CreateEmptyMessage();

  InterceptRequest<fidl::test::frobinator::Frobinator>(
      message, [](fidl::InterfacePtr<fidl::test::frobinator::Frobinator>& ptr) {
        ptr->Grob("one", [](fidl::StringPtr value) { FAIL(); });
      });

  fidl_message_header_t header = message.header();

  const std::vector<const InterfaceMethod*>* methods = loader_->GetByOrdinal(header.ordinal);
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

  std::unique_ptr<fidlcat::Object> decoded_request;
  fidlcat::DecodeRequest(method, message.bytes().data(), message.bytes().size(), handle_infos,
                         message.handles().size(), &decoded_request);
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
// |_iface| is the interface method name on examples::FidlcatTestInterface
//    (TODO: generalize which interface to use)
// |_json_value| is the expected JSON representation of the message.
// |_pretty_print| is the expected pretty print of the message.
// The remaining parameters are the parameters to |_iface| to generate the
// message.
#define TEST_DECODE_WIRE_BODY(_iface, _json_value, _pretty_print, ...)                             \
  do {                                                                                             \
    fidl::MessageBuffer buffer;                                                                    \
    fidl::Message message = buffer.CreateEmptyMessage();                                           \
    using test::fidlcat::examples::FidlcatTestInterface;                                           \
    InterceptRequest<FidlcatTestInterface>(                                                        \
        message,                                                                                   \
        [&](fidl::InterfacePtr<FidlcatTestInterface>& ptr) { ptr->_iface(__VA_ARGS__); });         \
                                                                                                   \
    fidl_message_header_t header = message.header();                                               \
                                                                                                   \
    const std::vector<const InterfaceMethod*>* methods = loader_->GetByOrdinal(header.ordinal);    \
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
                                                                                                   \
    MessageDecoder decoder(message.bytes().data(), message.bytes().size(), handle_infos,           \
                           message.handles().size(), /*output_errors=*/true);                      \
    std::unique_ptr<Object> object = decoder.DecodeMessage(*method->request());                    \
    ASSERT_FALSE(decoder.HasError()) << "Could not decode message";                                \
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
      object->PrettyPrint(result, FakeColors, "", 0, 80, 80);                                      \
    }                                                                                              \
    ASSERT_EQ(result.str(), _pretty_print)                                                         \
        << "expected = " << _pretty_print << " actual = " << result.str();                         \
                                                                                                   \
    for (uint32_t actual = 0; actual < message.bytes().actual(); ++actual) {                       \
      MessageDecoder decoder(message.bytes().data(), actual, handle_infos,                         \
                             message.handles().size(), /*output_errors=*/false);                   \
      std::unique_ptr<Object> object = decoder.DecodeMessage(*method->request());                  \
      ASSERT_TRUE(decoder.HasError()) << "expect decoder error for buffer size " << actual         \
                                      << " instead of " << message.bytes().actual();               \
    }                                                                                              \
                                                                                                   \
    for (uint32_t actual = 0; message.handles().actual() > actual; actual++) {                     \
      MessageDecoder decoder(message.bytes().data(), message.bytes().size(), handle_infos, actual, \
                             /*output_errors=*/false);                                             \
      std::unique_ptr<Object> object = decoder.DecodeMessage(*method->request());                  \
      ASSERT_TRUE(decoder.HasError()) << "expect decoder error for handle size " << actual         \
                                      << " instead of " << message.handles().actual();             \
    }                                                                                              \
    delete[] handle_infos;                                                                         \
  } while (0)

// This is a convenience wrapper for calling TEST_DECODE_WIRE_BODY that simply
// executes the code in a test.
// |_testname| is the name of the test (prepended by Parse in the output)
// |_iface| is the interface method name on examples::FidlcatTestInterface
//    (TODO: generalize which interface to use)
// |_json_value| is the expected JSON representation of the message.
// |_pretty_print| is the expected pretty print of the message.
// The remaining parameters are the parameters to |_iface| to generate the
// message.
#define TEST_DECODE_WIRE(_testname, _iface, _json_value, _pretty_print, ...) \
  TEST_F(WireParserTest, Parse##_testname) {                                 \
    TEST_DECODE_WIRE_BODY(_iface, _json_value, _pretty_print, __VA_ARGS__);  \
  }

// Scalar Tests

namespace {

template <class T>
std::string FieldToJson(std::string key, T value) {
  return "\"" + key + "\":\"" + std::to_string(value) + "\"";
}
template <>
std::string FieldToJson(std::string key, bool value) {
  return "\"" + key + "\":\"" + (value ? "true" : "false") + "\"";
}
template <>
std::string FieldToJson(std::string key, const char* value) {
  return "\"" + key + "\":\"" + value + "\"";
}
template <>
std::string FieldToJson(std::string key, std::string value) {
  return "\"" + key + "\":\"" + value + "\"";
}

template <class T>
std::string SingleToJson(std::string key, T value) {
  return "{ " + FieldToJson(key, value) + " }";
}

template <class T>
std::string FieldToPretty(std::string key, std::string type, T value) {
  return key + ": #gre#" + type + "#rst# = #blu#" + std::to_string(value) + "#rst#";
}
template <>
std::string FieldToPretty(std::string key, std::string type, bool value) {
  return key + ": #gre#" + type + "#rst# = #blu#" + (value ? "true" : "false") + "#rst#";
}
template <>
std::string FieldToPretty(std::string key, std::string type, const char* value) {
  return key + ": #gre#" + type + "#rst# = #red#\"" + value + "\"#rst#";
}
template <>
std::string FieldToPretty(std::string key, std::string type, std::string value) {
  return key + ": #gre#" + type + "#rst# = #red#\"" + value + "\"#rst#";
}

std::string HandleToJson(std::string key, zx_handle_t value) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(8) << value << std::dec << std::setw(0);
  return "\"" + key + "\":\"ZX_OBJ_TYPE_CHANNEL:" + ss.str() +
         "(ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | " +
         "ZX_RIGHT_SIGNAL_PEER | ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT)\"";
}
std::string HandleToPretty(std::string key, zx_handle_t value) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(8) << value << std::dec << std::setw(0);
  return key + ": #gre#handle#rst# = #red#ZX_OBJ_TYPE_CHANNEL:" + ss.str() + "#blu#" +
         "(ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | " +
         "ZX_RIGHT_SIGNAL_PEER | ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT)" + "#rst#";
}

template <class T>
std::string SingleToPretty(std::string key, std::string type, T value) {
  return "{ " + FieldToPretty(key, type, value) + " }";
}

}  // namespace

#define TEST_SINGLE(_testname, _iface, _key, _type, _value)        \
  TEST_DECODE_WIRE(_testname, _iface, SingleToJson(#_key, _value), \
                   SingleToPretty(#_key, #_type, _value), _value)

TEST_DECODE_WIRE(Empty, Empty, "{}", "{}")

TEST_SINGLE(String, String, s, string, "Hello World!")

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

TEST_SINGLE(Float32, Float32, f32, float32, 0.25)
TEST_SINGLE(Float64, Float64, f64, float64, 9007199254740992.0)

TEST_DECODE_WIRE(TwoTuple, Complex, R"({"real":"1", "imaginary":"2"})",
                 "{ " + FieldToPretty("real", "int32", 1) + ", " +
                     FieldToPretty("imaginary", "int32", 2) + " }",
                 1, 2);

TEST_DECODE_WIRE(StringInt, StringInt, R"({"s":"groucho", "i32":"4"})",
                 "{ " + FieldToPretty("s", "string", "groucho") + ", " +
                     FieldToPretty("i32", "int32", 4) + " }",
                 "groucho", 4)

// Vector / Array Tests

namespace {
int32_t one_param[1] = {1};
int32_t two_params[2] = {1, 2};

template <class T, size_t N>
std::array<T, N> ToArray(T ts[N]) {
  ::std::array<T, N> ret;
  std::copy_n(&ts[0], N, ret.begin());
  return ret;
}

// Converts an array to a VectorPtr, so that it can be passed to a FIDL
// interface.
template <class T>
fidl::VectorPtr<T> ToVector(T ts[], size_t n) {
  std::vector<T> vec;
  for (size_t i = 0; i < n; i++) {
    vec.push_back(ts[i]);
  }
  ::fidl::VectorPtr<T> ret(vec);
  return ret;
}

}  // namespace

TEST_DECODE_WIRE(Array1, Array1, R"({"b_1":["1"]})",
                 "{ b_1: #gre#array<int32>#rst# = [ #blu#1#rst# ] }",
                 (ToArray<int32_t, 1>(one_param)));

TEST_DECODE_WIRE(Array2, Array2, R"({"b_2":["1", "2"]})",
                 "{ b_2: #gre#array<int32>#rst# = [ #blu#1#rst#, #blu#2#rst# ] }",
                 (ToArray<int32_t, 2>(two_params)));

TEST_DECODE_WIRE(NullVector, Vector, R"({"v_1": null})",
                 "{ v_1: #gre#vector<int32>#rst# = #blu#null#rst# }", nullptr)

TEST_DECODE_WIRE(VectorOneElt, Vector, R"({"v_1":["1"]})",
                 "{ v_1: #gre#vector<int32>#rst# = [ #blu#1#rst# ] }",
                 (ToVector<int32_t>(one_param, 1)));

TEST_DECODE_WIRE(VectorTwoElt, Vector, R"({"v_1":["1", "2"]})",
                 "{ v_1: #gre#vector<int32>#rst# = [ #blu#1#rst#, #blu#2#rst# ] }",
                 (ToVector<int32_t>(two_params, 2)));

namespace {

std::array<std::string, 2> TwoStringArrayFromVals(std::string v1, std::string v2) {
  std::array<std::string, 2> brother_array;
  brother_array[0] = v1;
  brother_array[1] = v2;
  return brother_array;
}

}  // namespace

TEST_DECODE_WIRE(TwoStringArrayInt, TwoStringArrayInt, R"({"arr":["harpo","chico"], "i32":"1"})",
                 R"({ arr: #gre#array<string>#rst# = [ #red#"harpo"#rst#, #red#"chico"#rst# ], )" +
                     FieldToPretty("i32", "int32", 1) + " }",
                 TwoStringArrayFromVals("harpo", "chico"), 1)

namespace {

std::vector<std::string> TwoStringVectorFromVals(std::string v1, std::string v2) {
  return std::vector<std::string>({v1, v2});
}

std::vector<uint8_t> VectorUint8() {
  std::vector<std::uint8_t> result;
  for (int i = 0; i <= 40; ++i) {
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
  std::vector<std::uint32_t> result;
  for (int i = 0; i <= 25; ++i) {
    result.push_back(i + ((i & 1) << 16));
  }
  return result;
}

}  // namespace

TEST_DECODE_WIRE(TwoStringVectorInt, TwoStringVectorInt, R"({"vec":["harpo", "chico"], "i32":"1"})",
                 R"({ vec: #gre#vector<string>#rst# = [ #red#"harpo"#rst#, #red#"chico"#rst# ], )" +
                     FieldToPretty("i32", "int32", 1) + " }",
                 TwoStringVectorFromVals("harpo", "chico"), 1)

TEST_DECODE_WIRE(TwoStringVectors, TwoStringVectors,
                 R"({"v_1":["harpo","chico"],"v_2":["groucho","zeppo"]})",
                 "{\n  v_1: #gre#vector<string>#rst# = "
                 R"([ #red#"harpo"#rst#, #red#"chico"#rst# ])"
                 "\n  v_2: #gre#vector<string>#rst# = "
                 R"([ #red#"groucho"#rst#, #red#"zeppo"#rst# ])"
                 "\n}",
                 TwoStringVectorFromVals("harpo", "chico"),
                 TwoStringVectorFromVals("groucho", "zeppo"))

TEST_DECODE_WIRE(
    VectorUint8, VectorUint8,
    R"({"v":["0","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32","33","34","35","36","37","38","39","40"]})",
    "{\n"
    "  v: #gre#vector<uint8>#rst# = [\n"
    "    #blu#0#rst#, #blu#1#rst#, #blu#2#rst#, #blu#3#rst#, #blu#4#rst#, #blu#5#rst#, "
    "#blu#6#rst#, #blu#7#rst#, #blu#8#rst#, #blu#9#rst#, #blu#10#rst#, #blu#11#rst#, #blu#12#rst#, "
    "#blu#13#rst#, #blu#14#rst#, #blu#15#rst#, #blu#16#rst#, #blu#17#rst#, #blu#18#rst#, "
    "#blu#19#rst#, #blu#20#rst#,\n"
    "    #blu#21#rst#, #blu#22#rst#, #blu#23#rst#, #blu#24#rst#, #blu#25#rst#, #blu#26#rst#, "
    "#blu#27#rst#, #blu#28#rst#, #blu#29#rst#, #blu#30#rst#, #blu#31#rst#, #blu#32#rst#, "
    "#blu#33#rst#, #blu#34#rst#, #blu#35#rst#, #blu#36#rst#, #blu#37#rst#, #blu#38#rst#, "
    "#blu#39#rst#,\n"
    "    #blu#40#rst#\n"
    "  ]\n"
    "}",
    VectorUint8())

TEST_DECODE_WIRE(
    VectorUint8String, VectorUint8,
    R"({"v":["72","101","108","108","111","32","116","101","115","116","105","110","103","32","119","111","114","108","100","33"]})",
    "{ v: #gre#vector<uint8>#rst# = \"Hello testing world!\" }",
    VectorUint8("Hello testing world!"))

TEST_DECODE_WIRE(
    VectorUint8MultilineString, VectorUint8,
    R"({"v":["72","101","108","108","111","32","116","101","115","116","105","110","103","32","119","111","114","108","100","33","10","72","111","119","32","97","114","101","32","121","111","117","32","116","111","100","97","121","63","10","73","39","109","32","116","101","115","116","105","110","103","32","102","105","100","108","99","97","116","46"]})",
    "{\n"
    "  v: #gre#vector<uint8>#rst# = [\n"
    "    Hello testing world!\n"
    "    How are you today?\n"
    "    I'm testing fidlcat.\n"
    "  ]\n"
    "}",
    VectorUint8("Hello testing world!\nHow are you today?\nI'm testing fidlcat."))

TEST_DECODE_WIRE(
    VectorUint32, VectorUint32,
    R"({"v":["0","65537","2","65539","4","65541","6","65543","8","65545","10","65547","12","65549","14","65551","16","65553","18","65555","20","65557","22","65559","24","65561"]})",
    "{\n"
    "  v: #gre#vector<uint32>#rst# = [\n"
    "    #blu#0#rst#, #blu#65537#rst#, #blu#2#rst#, #blu#65539#rst#, #blu#4#rst#, #blu#65541#rst#, "
    "#blu#6#rst#, #blu#65543#rst#, #blu#8#rst#, #blu#65545#rst#, #blu#10#rst#, #blu#65547#rst#, "
    "#blu#12#rst#, #blu#65549#rst#, #blu#14#rst#,\n"
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
    pt.f32 = 0.25;
    pt.f64 = 9007199254740992.0;
  }

  std::string GetJson() {
    std::ostringstream es;
    es << R"({"p":{)" << FieldToJson("s", pt.s) << "," << FieldToJson("b", pt.b) << ","
       << FieldToJson("i8", pt.i8) << "," << FieldToJson("i16", pt.i16) << ","
       << FieldToJson("i32", pt.i32) << "," << FieldToJson("i64", pt.i64) << ","
       << FieldToJson("u8", pt.u8) << "," << FieldToJson("u16", pt.u16) << ","
       << FieldToJson("u32", pt.u32) << "," << FieldToJson("u64", pt.u64) << ","
       << FieldToJson("f32", pt.f32) << "," << FieldToJson("f64", pt.f64) << "}}";
    return es.str();
  }
  std::string GetPretty() {
    std::ostringstream es;
    es << "{\n"
       << "  p: #gre#test.fidlcat.examples/primitive_types#rst# = {\n"
       << "    " << FieldToPretty("s", "string", pt.s) << "\n"
       << "    " << FieldToPretty("b", "bool", pt.b) << "\n"
       << "    " << FieldToPretty("i8", "int8", pt.i8) << "\n"
       << "    " << FieldToPretty("i16", "int16", pt.i16) << "\n"
       << "    " << FieldToPretty("i32", "int32", pt.i32) << "\n"
       << "    " << FieldToPretty("i64", "int64", pt.i64) << "\n"
       << "    " << FieldToPretty("u8", "uint8", pt.u8) << "\n"
       << "    " << FieldToPretty("u16", "uint16", pt.u16) << "\n"
       << "    " << FieldToPretty("u32", "uint32", pt.u32) << "\n"
       << "    " << FieldToPretty("u64", "uint64", pt.u64) << "\n"
       << "    " << FieldToPretty("f32", "float32", pt.f32) << "\n"
       << "    " << FieldToPretty("f64", "float64", pt.f64) << "\n"
       << "  }\n"
       << "}";
    return es.str();
  }

  test::fidlcat::examples::primitive_types pt;
};

}  // namespace

TEST_F(WireParserTest, ParseStruct) {
  StructSupport sd;
  TEST_DECODE_WIRE_BODY(Struct, sd.GetJson(), sd.GetPretty(), sd.pt);
}

TEST_DECODE_WIRE(NullableStruct, NullableStruct, R"({"p":null})",
                 "{ p: #gre#test.fidlcat.examples/primitive_types#rst# = #blu#null#rst# }",
                 nullptr);

TEST_DECODE_WIRE(NullableStructAndInt, NullableStructAndInt, R"({"p":null, "i":"1"})",
                 "{ p: #gre#test.fidlcat.examples/primitive_types#rst# = "
                 "#blu#null#rst#, i: #gre#int32#rst# = #blu#1#rst# }",
                 nullptr, 1);

namespace {

test::fidlcat::examples::two_string_struct TwoStringStructFromVals(std::string v1, std::string v2) {
  test::fidlcat::examples::two_string_struct tss;
  tss.value1 = v1;
  tss.value2 = v2;
  return tss;
}

std::unique_ptr<test::fidlcat::examples::two_string_struct> TwoStringStructFromValsPtr(
    std::string v1, std::string v2) {
  std::unique_ptr<test::fidlcat::examples::two_string_struct> ptr(
      new test::fidlcat::examples::two_string_struct());
  ptr->value1 = v1;
  ptr->value2 = v2;
  return ptr;
}

std::string TwoStringStructIntPretty(const char* s1, const char* s2, int v) {
  std::string result = "{\n  s: #gre#test.fidlcat.examples/two_string_struct#rst# = {\n";
  result += "    " + FieldToPretty("value1", "string", s1) + "\n";
  result += "    " + FieldToPretty("value2", "string", s2) + "\n";
  result += "  }\n";
  result += "  " + FieldToPretty("i32", "int32", v) + "\n";
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

// TODO: Add the following struct tests:
// struct{uint8 f1; uint32 f2;}
// struct{struct{uint8 f1; uint32 f2;} inner; uint8 f3;}

// Union and XUnion tests

namespace {

using isu = test::fidlcat::examples::int_struct_union;
using xisu = test::fidlcat::examples::int_struct_xunion;

template <class T>
T GetIntUnion(int32_t i) {
  T u;
  u.set_variant_i(i);
  return u;
}

template <class T>
T GetStructUnion(std::string v1, std::string v2) {
  T u;
  test::fidlcat::examples::two_string_struct tss = TwoStringStructFromVals(v1, v2);
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
std::unique_ptr<T> GetStructUnionPtr(std::string v1, std::string v2) {
  std::unique_ptr<T> ptr(new T());
  test::fidlcat::examples::two_string_struct tss = TwoStringStructFromVals(v1, v2);
  ptr->set_variant_tss(tss);
  return ptr;
}

std::string IntUnionIntPretty(std::string name, int u, int v) {
  std::string result = "{\n";
  result += "  isu: #gre#test.fidlcat.examples/" + name + "#rst# = { " +
            FieldToPretty("variant_i", "int32", u) + " }\n";
  result += "  " + FieldToPretty("i", "int32", v) + "\n";
  result += "}";
  return result;
}

std::string StructUnionIntPretty(std::string name, const char* u1, const char* u2, int v) {
  std::string result = "{\n";
  result += "  isu: #gre#test.fidlcat.examples/" + name + "#rst# = {\n";
  result +=
      "    variant_tss: #gre#test.fidlcat.examples/two_string_struct#rst# = "
      "{\n";
  result += "      " + FieldToPretty("value1", "string", u1) + "\n";
  result += "      " + FieldToPretty("value2", "string", u2) + "\n";
  result += "    }\n";
  result += "  }\n";
  result += "  " + FieldToPretty("i", "int32", v) + "\n";
  result += "}";
  return result;
}

std::string IntIntUnionPretty(std::string name, int v, int u) {
  std::string result = "{\n";
  result += "  " + FieldToPretty("i", "int32", v) + "\n";
  result += "  isu: #gre#test.fidlcat.examples/" + name + "#rst# = { " +
            FieldToPretty("variant_i", "int32", u) + " }\n";
  result += "}";
  return result;
}

std::string IntStructUnionPretty(std::string name, int v, const char* u1, const char* u2) {
  std::string result = "{\n";
  result += "  " + FieldToPretty("i", "int32", v) + "\n";
  result += "  isu: #gre#test.fidlcat.examples/" + name + "#rst# = {\n";
  result +=
      "    variant_tss: #gre#test.fidlcat.examples/two_string_struct#rst# = "
      "{\n";
  result += "      " + FieldToPretty("value1", "string", u1) + "\n";
  result += "      " + FieldToPretty("value2", "string", u2) + "\n";
  result += "    }\n";
  result += "  }\n";
  result += "}";
  return result;
}

}  // namespace

TEST_DECODE_WIRE(UnionInt, Union, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                 IntUnionIntPretty("int_struct_union", 42, 1), GetIntUnion<isu>(42), 1);

TEST_DECODE_WIRE(UnionStruct, Union,
                 R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
                 StructUnionIntPretty("int_struct_union", "harpo", "chico", 1),
                 GetStructUnion<isu>("harpo", "chico"), 1);

TEST_DECODE_WIRE(NullableUnionInt, NullableUnion, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                 IntUnionIntPretty("int_struct_union", 42, 1), GetIntUnionPtr<isu>(42), 1);

TEST_DECODE_WIRE(NullableUnionStruct, NullableUnion,
                 R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
                 StructUnionIntPretty("int_struct_union", "harpo", "chico", 1),
                 GetStructUnionPtr<isu>("harpo", "chico"), 1);

TEST_DECODE_WIRE(NullableUnionIntFirstInt, NullableUnionIntFirst,
                 R"({"i" : "1", "isu":{"variant_i":"42"}})",
                 IntIntUnionPretty("int_struct_union", 1, 42), 1, GetIntUnionPtr<isu>(42));

TEST_DECODE_WIRE(NullableUnionIntFirstStruct, NullableUnionIntFirst,
                 R"({"i": "1", "isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}})",
                 IntStructUnionPretty("int_struct_union", 1, "harpo", "chico"), 1,
                 GetStructUnionPtr<isu>("harpo", "chico"));

TEST_DECODE_WIRE(XUnionInt, XUnion, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                 IntUnionIntPretty("int_struct_xunion", 42, 1), GetIntUnion<xisu>(42), 1);

TEST_DECODE_WIRE(XUnionStruct, XUnion,
                 R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
                 StructUnionIntPretty("int_struct_xunion", "harpo", "chico", 1),
                 GetStructUnion<xisu>("harpo", "chico"), 1);

TEST_DECODE_WIRE(NullableXUnionInt, NullableXUnion, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                 IntUnionIntPretty("int_struct_xunion", 42, 1), GetIntUnionPtr<xisu>(42), 1);

TEST_DECODE_WIRE(NullableXUnionStruct, NullableXUnion,
                 R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
                 StructUnionIntPretty("int_struct_xunion", "harpo", "chico", 1),
                 GetStructUnionPtr<xisu>("harpo", "chico"), 1);

TEST_DECODE_WIRE(NullableXUnionIntFirstInt, NullableXUnionIntFirst,
                 R"({"i" : "1", "isu":{"variant_i":"42"}})",
                 IntIntUnionPretty("int_struct_xunion", 1, 42), 1, GetIntUnionPtr<xisu>(42));

TEST_DECODE_WIRE(NullableXUnionIntFirstStruct, NullableXUnionIntFirst,
                 R"({"i": "1", "isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}})",
                 IntStructUnionPretty("int_struct_xunion", 1, "harpo", "chico"), 1,
                 GetStructUnionPtr<xisu>("harpo", "chico"));

namespace {

using uuu = test::fidlcat::examples::u8_u16_union;
using uux = test::fidlcat::examples::u8_u16_xunion;

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

std::string ShortUnionPretty(std::string name, const char* field, const char* type, int u, int v) {
  std::string result = "{\n";
  result += "  u: #gre#test.fidlcat.examples/" + name + "#rst# = { " +
            FieldToPretty(field, type, u) + " }\n";
  result += "  " + FieldToPretty("i", "int32", v) + "\n";
  result += "}";
  return result;
}

}  // namespace

TEST_DECODE_WIRE(ShortUnion8, ShortUnion, R"({"u":{"variant_u8":"16"}, "i":"1"})",
                 ShortUnionPretty("u8_u16_union", "variant_u8", "uint8", 16, 1),
                 GetUInt8Union<uuu>(16), 1);

TEST_DECODE_WIRE(ShortUnion16, ShortUnion, R"({"u":{"variant_u16":"1024"}, "i":"1"})",
                 ShortUnionPretty("u8_u16_union", "variant_u16", "uint16", 1024, 1),
                 GetUInt16Union<uuu>(1024), 1);

TEST_DECODE_WIRE(ShortXUnion8, ShortXUnion, R"({"u":{"variant_u8":"16"}, "i":"1"})",
                 ShortUnionPretty("u8_u16_xunion", "variant_u8", "uint8", 16, 1),
                 GetUInt8Union<uux>(16), 1);

TEST_DECODE_WIRE(ShortXUnion16, ShortXUnion, R"({"u":{"variant_u16":"1024"}, "i":"1"})",
                 ShortUnionPretty("u8_u16_xunion", "variant_u16", "uint16", 1024, 1),
                 GetUInt16Union<uux>(1024), 1);

// Enum Tests

TEST_DECODE_WIRE(DefaultEnum, DefaultEnum, R"({"ev":"x"})",
                 "{ ev: #gre#test.fidlcat.examples/default_enum#rst# = #blu#x#rst# }",
                 test::fidlcat::examples::default_enum::x);
TEST_DECODE_WIRE(I8Enum, I8Enum, R"({"ev":"x"})",
                 "{ ev: #gre#test.fidlcat.examples/i8_enum#rst# = #blu#x#rst# }",
                 test::fidlcat::examples::i8_enum::x);
TEST_DECODE_WIRE(I16Enum, I16Enum, R"({"ev":"x"})",
                 "{ ev: #gre#test.fidlcat.examples/i16_enum#rst# = #blu#x#rst# }",
                 test::fidlcat::examples::i16_enum::x);

// Table Tests

test::fidlcat::examples::value_table GetTable(std::optional<int16_t> first_int16,
                                              std::optional<std::string> value1,
                                              std::optional<std::string> value2,
                                              std::optional<int32_t> third_union_val) {
  test::fidlcat::examples::value_table table;
  if (first_int16.has_value()) {
    table.set_first_int16(*first_int16);
  }
  if (value1.has_value()) {
    table.set_second_struct(TwoStringStructFromVals(*value1, *value2));
  }
  if (third_union_val.has_value()) {
    test::fidlcat::examples::int_struct_union u;
    u.set_variant_i(*third_union_val);
    table.set_third_union(std::move(u));
  }
  return table;
}

std::string TablePretty(std::optional<int16_t> first_int16, std::optional<std::string> value1,
                        std::optional<std::string> value2, std::optional<int32_t> third_union_val,
                        int i) {
  if (!first_int16.has_value() && !value1.has_value() && !third_union_val.has_value()) {
    std::string result = "{ table: #gre#test.fidlcat.examples/value_table#rst# = {}, ";
    result += FieldToPretty("i", "int32", i);
    result += " }";
    return result;
  }
  std::string result = "{\n";
  if (!value1.has_value() && !third_union_val.has_value()) {
    result += "  table: #gre#test.fidlcat.examples/value_table#rst# = { ";
    result += FieldToPretty("first_int16", "int16", *first_int16) + " }\n";
  } else {
    result += "  table: #gre#test.fidlcat.examples/value_table#rst# = {\n";
    if (first_int16.has_value()) {
      result += "    " + FieldToPretty("first_int16", "int16", *first_int16) + "\n";
    }
    if (value1.has_value()) {
      result +=
          "    second_struct: "
          "#gre#test.fidlcat.examples/two_string_struct#rst# = {\n";
      result += "      " + FieldToPretty("value1", "string", *value1) + "\n";
      result += "      " + FieldToPretty("value2", "string", *value2) + "\n";
      result += "    }\n";
    }
    if (third_union_val.has_value()) {
      result +=
          "    third_union: #gre#test.fidlcat.examples/int_struct_union#rst# = "
          "{\n";
      result += "      " + FieldToPretty("variant_i", "int32", *third_union_val) + "\n";
      result += "    }\n";
    }
    result += "  }\n";
  }
  result += "  " + FieldToPretty("i", "int32", i) + "\n";
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

// TODO(DX-1476): Add a test that exercises what happens when we encounter an
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
                        support.interface<test::fidlcat::examples::ParamProtocol>());
}

namespace {

class HandleStructSupport {
 public:
  HandleStructSupport() {
    zx::channel::create(0, &out1_, &out2_);
    zx::channel::create(0, &out3_, &out4_);
    json_ = "{\"hs\":{" + HandleToJson("h1", out1_.get()) + "," + HandleToJson("h2", out2_.get()) +
            "," + HandleToJson("h3", out3_.get()) + "}}";
    pretty_ = "{\n  hs: #gre#test.fidlcat.examples/handle_struct#rst# = {\n    " +
              HandleToPretty("h1", out1_.get()) + "\n    " + HandleToPretty("h2", out2_.get()) +
              "\n    " + HandleToPretty("h3", out3_.get()) + "\n  }\n}";
  }
  test::fidlcat::examples::handle_struct handle_struct() {
    test::fidlcat::examples::handle_struct hs;
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
  TEST_DECODE_WIRE_BODY(HandleStruct, support.GetJSON(), support.GetPretty(),
                        support.handle_struct());
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
        "{\n  t: #gre#test.fidlcat.examples/traversal_order#rst# = {\n    "
        "s: #gre#test.fidlcat.examples/opt_handle_struct#rst# = {\n      " +
        HandleToPretty("sh1", sh1_.get()) + "\n      " + HandleToPretty("sh2", sh2_.get()) +
        "\n    }\n    " + HandleToPretty("h1", h1_.get()) + "\n    " +
        HandleToPretty("h2", h2_.get()) + "\n  }\n}";
  }
  test::fidlcat::examples::traversal_order traversal_order() {
    test::fidlcat::examples::traversal_order t;
    auto s = std::make_unique<test::fidlcat::examples::opt_handle_struct>();
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
  TEST_DECODE_WIRE_BODY(TraversalOrder, support.GetJSON(), support.GetPretty(),
                        support.traversal_order());
}

// Corrupt data tests

TEST_F(WireParserTest, BadSchemaPrintHex) {
  std::vector<std::unique_ptr<std::istream>> library_files;
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
      "name": "test.fidlcat.examples/FidlcatTestInterface",
      "location": {
        "filename": "../../tools/fidlcat/lib/testdata/types.test.fidl",
        "line": 11,
        "column": 10
      },
      "methods": [
        {
          "ordinal": 1625951384,
          "generated_ordinal": 1625951384,
          "name": "Int32",
          "location": {
            "filename": "../../tools/fidlcat/lib/testdata/types.test.fidl",
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
                "filename": "../../tools/fidlcat/lib/testdata/types.test.fidl",
                "line": 16,
                "column": 17
              },
              "size": 4,
              "max_out_of_line": 0,
              "alignment": 4,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_request_size": 24,
          "maybe_request_alignment": 8,
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
  std::unique_ptr<std::istream> file =
      std::make_unique<std::istringstream>(std::istringstream(bad_schema));
  library_files.push_back(std::move(file));
  LibraryReadError err;
  LibraryLoader loader(library_files, &err);
  ASSERT_TRUE(err.value == LibraryReadError::ErrorValue::kOk);
  fidl::MessageBuffer buffer;
  fidl::Message message = buffer.CreateEmptyMessage();

  InterceptRequest<test::fidlcat::examples::FidlcatTestInterface>(
      message, [](fidl::InterfacePtr<test::fidlcat::examples::FidlcatTestInterface>& ptr) {
        ptr->Int32(0xdeadbeef);
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

  std::unique_ptr<fidlcat::Object> decoded_request;
  fidlcat::DecodeRequest(method, message.bytes().data(), message.bytes().size(), handle_infos,
                         message.handles().size(), &decoded_request);
  rapidjson::Document actual;
  if (decoded_request != nullptr) {
    decoded_request->ExtractJson(actual.GetAllocator(), actual);
  }

  ASSERT_STREQ(actual["i32"].GetString(), "ef be ad de");

  delete[] handle_infos;
}

}  // namespace fidlcat
