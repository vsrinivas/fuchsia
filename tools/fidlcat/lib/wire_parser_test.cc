// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_parser.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include <iostream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"
#include "library_loader.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "test/fidlcat/examples/cpp/fidl.h"
#include "tools/fidlcat/lib/library_loader_test_data.h"

namespace fidlcat {

// Stolen from //sdk/lib/fidl/cpp/test/async_loop_for_test.{h,cc}; cc
// is not public

class AsyncLoopForTestImpl;

class AsyncLoopForTest {
 public:
  // The AsyncLoopForTest constructor should also call
  // async_set_default_dispatcher() with the chosen dispatcher implementation.
  AsyncLoopForTest();
  ~AsyncLoopForTest();

  // This call matches the behavior of async_loop_run_until_idle().
  zx_status_t RunUntilIdle();

  // This call matches the behavior of async_loop_run().
  zx_status_t Run();

  // Returns the underlying async_t.
  async_dispatcher_t* dispatcher();

 private:
  std::unique_ptr<AsyncLoopForTestImpl> impl_;
};

class AsyncLoopForTestImpl {
 public:
  AsyncLoopForTestImpl() : loop_(&kAsyncLoopConfigAttachToThread) {}
  ~AsyncLoopForTestImpl() = default;

  async::Loop* loop() { return &loop_; }

 private:
  async::Loop loop_;
};

AsyncLoopForTest::AsyncLoopForTest()
    : impl_(std::make_unique<AsyncLoopForTestImpl>()) {}

AsyncLoopForTest::~AsyncLoopForTest() = default;

zx_status_t AsyncLoopForTest::RunUntilIdle() {
  return impl_->loop()->RunUntilIdle();
}

zx_status_t AsyncLoopForTest::Run() { return impl_->loop()->Run(); }

async_dispatcher_t* AsyncLoopForTest::dispatcher() {
  return impl_->loop()->dispatcher();
}

LibraryLoader* InitLoader() {
  std::vector<std::unique_ptr<std::istream>> library_files;
  fidlcat_test::ExampleMap examples;
  for (auto element : examples.map()) {
    std::unique_ptr<std::istream> file = std::make_unique<std::istringstream>(
        std::istringstream(element.second));
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

// The tests in this file work the following way:
// 1) Create a channel.
// 2) Bind an interface pointer to the client side of that channel.
// 3) Listen at the other end of the channel for the message.
// 4) Convert the message to JSON using the JSON message converter, and check
//    that the results look as expected.

// This binds |invoke| to one end of a channel, invokes it, and drops the wire
// format bits it picks up off the other end into |message|.
template <class T>
void InterceptRequest(fidl::Message& message,
                      std::function<void(fidl::InterfacePtr<T>&)> invoke) {
  AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  fidl::InterfacePtr<T> ptr;
  int error_count = 0;
  ptr.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
  });

  EXPECT_EQ(ZX_OK, ptr.Bind(std::move(h1)));

  invoke(ptr);

  loop.RunUntilIdle();

  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
}

TEST_F(WireParserTest, ParseSingleString) {
  fidl::MessageBuffer buffer;
  fidl::Message message = buffer.CreateEmptyMessage();

  InterceptRequest<fidl::test::frobinator::Frobinator>(
      message, [](fidl::InterfacePtr<fidl::test::frobinator::Frobinator>& ptr) {
        ptr->Grob("one", [](fidl::StringPtr value) { FAIL(); });
      });

  fidl_message_header_t header = message.header();

  const InterfaceMethod* method;
  ASSERT_TRUE(loader_->GetByOrdinal(header.ordinal, &method));
  ASSERT_EQ("Grob", method->name());
  rapidjson::Document actual;
  fidlcat::RequestToJSON(method, message, actual);

  rapidjson::Document expected;
  expected.Parse<rapidjson::kParseNumbersAsStringsFlag>(
      R"JSON({"value":"one"})JSON");
  ASSERT_EQ(expected, actual);
}

// This is a general-purpose macro for calling InterceptRequest and checking its
// results.  It can be generalized to a wide variety of types (and is, below).
// It checks for successful parsing, as well as failure when parsing truncated
// values.
// _iface is the interface method name on examples::this_is_an_interface
//    (TODO: generalize which interface to use)
// _json_value is a JSON representation of the value in the previous parameter.
// The remaining parameters are the parameters to _iface to generate the
// _json_value.
#define TEST_WIRE_TO_JSON_BODY(_iface, _json_value, ...)                       \
  do {                                                                         \
    fidl::MessageBuffer buffer;                                                \
    fidl::Message message = buffer.CreateEmptyMessage();                       \
    using test::fidlcat::examples::this_is_an_interface;                       \
    InterceptRequest<this_is_an_interface>(                                    \
        message, [&](fidl::InterfacePtr<this_is_an_interface>& ptr) {          \
          ptr->_iface(__VA_ARGS__);                                            \
        });                                                                    \
                                                                               \
    fidl_message_header_t header = message.header();                           \
                                                                               \
    const InterfaceMethod* method;                                             \
    ASSERT_TRUE(loader_->GetByOrdinal(header.ordinal, &method));               \
    ASSERT_EQ(#_iface, method->name());                                        \
                                                                               \
    rapidjson::Document actual;                                                \
    ASSERT_TRUE(fidlcat::RequestToJSON(method, message, actual))               \
        << "Could not convert message to json";                                \
    rapidjson::StringBuffer actual_string;                                     \
    rapidjson::Writer<rapidjson::StringBuffer> actual_w(actual_string);        \
    actual.Accept(actual_w);                                                   \
                                                                               \
    rapidjson::Document expected;                                              \
    std::string expected_source = _json_value;                                 \
    expected.Parse<rapidjson::kParseNumbersAsStringsFlag>(                     \
        expected_source.c_str());                                              \
    rapidjson::StringBuffer expected_string;                                   \
    rapidjson::Writer<rapidjson::StringBuffer> expected_w(expected_string);    \
    expected.Accept(expected_w);                                               \
                                                                               \
    ASSERT_EQ(expected, actual)                                                \
        << "expected = " << expected_string.GetString() << " ("                \
        << expected_source << ")"                                              \
        << " and actual = " << actual_string.GetString();                      \
                                                                               \
    /* Note we do might not check the last few bytes - we could be done */     \
    /* parsing before end of the word-boundary aligned amount that was sent */ \
    /* over the wire. */                                                       \
    for (uint32_t actual = 0;                                                  \
         message.bytes().actual() > ((actual + 7) & (~7)); actual++) {         \
      fidl::HandlePart handles(message.handles().data(),                       \
                               message.handles().capacity(),                   \
                               message.handles().actual());                    \
      fidl::BytePart bytes(message.bytes().data(), message.bytes().capacity(), \
                           actual);                                            \
      fidl::Message message_copy(std::move(bytes), std::move(handles));        \
      rapidjson::Document doc;                                                 \
      ASSERT_FALSE(fidlcat::RequestToJSON(method, message_copy, doc))          \
          << "bytes storage = " << message.bytes().actual()                    \
          << " and succeeded when truncating to " << actual;                   \
    }                                                                          \
                                                                               \
    for (uint32_t actual = 0; message.handles().actual() > actual; actual++) { \
      fidl::HandlePart handles(message.handles().data(),                       \
                               message.handles().capacity(), actual);          \
      fidl::BytePart bytes(message.bytes().data(), message.bytes().capacity(), \
                           message.bytes().actual());                          \
      fidl::Message message_copy(std::move(bytes), std::move(handles));        \
      rapidjson::Document doc;                                                 \
      ASSERT_FALSE(fidlcat::RequestToJSON(method, message_copy, doc))          \
          << "handle storage = " << message.handles().actual()                 \
          << " and succeeded when truncating to " << actual;                   \
    }                                                                          \
  } while (0)

// This is a convenience wrapper for calling TEST_WIRE_TO_JSON_BODY that simply
// executes the code in a test.
// _testname is the name of the test (prepended by Parse in the output)
// _iface is the interface method name on examples::this_is_an_interface
//    (TODO: generalize which interface to use)
// _json_value is a JSON representation of the value in the previous parameter.
// The remaining parameters are the parameters to _iface to generate the
// _json_value.
#define TEST_WIRE_TO_JSON(_testname, _iface, _json_value, ...) \
  TEST_F(WireParserTest, Parse##_testname) {                   \
    TEST_WIRE_TO_JSON_BODY(_iface, _json_value, __VA_ARGS__);  \
  }

namespace {

std::string RawPair(std::string key, std::string value) {
  std::string result = "{\"";
  result.append(key);
  result.append("\":");
  result.append(value);
  result.append("}");
  return result;
}

}  // namespace

// Scalar Tests

namespace {

template <class T>
std::string SingleToJson(std::string key, T value) {
  return RawPair(key, "\"" + std::to_string(value) + "\"");
}

}  // namespace

#define TEST_SINGLE(_iface, _key, _value) \
  TEST_WIRE_TO_JSON(_iface, _iface, SingleToJson(#_key, _value), _value)

TEST_SINGLE(Float32, f32, 0.25)
TEST_SINGLE(Float64, f64, 9007199254740992.0)
TEST_SINGLE(Int8, i8, std::numeric_limits<int8_t>::min())
TEST_SINGLE(Int16, i16, std::numeric_limits<int16_t>::min())
TEST_SINGLE(Int32, i32, std::numeric_limits<int32_t>::min())
TEST_SINGLE(Int64, i64, std::numeric_limits<int64_t>::min())
TEST_SINGLE(Uint8, i8, std::numeric_limits<uint8_t>::max())
TEST_SINGLE(Uint16, i16, std::numeric_limits<uint16_t>::max())
TEST_SINGLE(Uint32, i32, std::numeric_limits<uint32_t>::max())
TEST_SINGLE(Uint64, i64, std::numeric_limits<uint64_t>::max())

TEST_WIRE_TO_JSON(SingleBool, Bool, R"({"b":"true"})", true)

TEST_WIRE_TO_JSON(TwoTuple, Complex, R"({"real":"1", "imaginary":"2"})", 1, 2);

TEST_WIRE_TO_JSON(StringInt, StringInt, R"({"s":"groucho", "i32":"4"})",
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

TEST_WIRE_TO_JSON(Array1, Array1, R"({"b_1":["1"]})",
                  (ToArray<int32_t, 1>(one_param)));

TEST_WIRE_TO_JSON(Array2, Array2, R"({"b_2":["1", "2"]})",
                  (ToArray<int32_t, 2>(two_params)));

TEST_WIRE_TO_JSON(VectorOneElt, Vector, R"({"v_1":["1"]})",
                  (ToVector<int32_t>(one_param, 1)));

std::string NullPair(std::string key, void* v) { return RawPair(key, "null"); }

TEST_WIRE_TO_JSON(NullVector, Vector, R"({"v_1": null})", nullptr)

namespace {

std::array<std::string, 2> TwoStringArrayFromVals(std::string v1,
                                                  std::string v2) {
  std::array<std::string, 2> brother_array;
  brother_array[0] = v1;
  brother_array[1] = v2;
  return brother_array;
}

}  // namespace

TEST_WIRE_TO_JSON(TwoStringArrayInt, TwoStringArrayInt,
                  R"({"arr":["harpo","chico"], "i32":"1"})",
                  TwoStringArrayFromVals("harpo", "chico"), 1)

namespace {

fidl::VectorPtr<std::string> TwoStringVectorFromVals(std::string v1,
                                                     std::string v2) {
  std::vector<std::string> brother_vector;
  brother_vector.push_back(v1);
  brother_vector.push_back(v2);
  return fidl::VectorPtr(brother_vector);
}

}  // namespace

TEST_WIRE_TO_JSON(TwoStringVectorInt, TwoStringVectorInt,
                  R"({"vec":["harpo", "chico"], "i32":"1"})",
                  TwoStringVectorFromVals("harpo", "chico"), 1)

// Struct Tests

namespace {

class StructSupport {
 public:
  StructSupport() {
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

  // TODO: It might be nice to have a more readable strategy for generating
  // JSON, e.g.,:
  // return GenerateObject([&]() {
  //  GenerateObjectMember("b", "true");
  //  GenerateObjectMember("i8", "-128");
  //  ...
  // });
  // Ideally, we'd also harmonize StructSupport and HandleStructSupport.
  std::string GetJSON() {
    std::ostringstream es;
    es << R"JSON({"p":{)JSON"
       << R"JSON("b":")JSON" << (pt.b ? "true" : "false") << R"JSON(",)JSON"
       << R"JSON("i8":")JSON" << std::to_string(pt.i8)
       << R"JSON(", "i16":")JSON" << std::to_string(pt.i16)
       << R"JSON(", "i32":")JSON" << std::to_string(pt.i32)
       << R"JSON(", "i64":")JSON" << std::to_string(pt.i64)
       << R"JSON(", "u8":")JSON" << std::to_string(pt.u8)
       << R"JSON(", "u16":")JSON" << std::to_string(pt.u16)
       << R"JSON(", "u32":")JSON" << std::to_string(pt.u32)
       << R"JSON(", "u64":")JSON" << std::to_string(pt.u64)
       << R"JSON(", "f32":")JSON" << std::to_string(pt.f32)
       << R"JSON(", "f64":")JSON" << std::to_string(pt.f64) << "\"}}";
    return es.str();
  }
  test::fidlcat::examples::primitive_types pt;
};

}  // namespace

TEST_F(WireParserTest, ParseStruct) {
  StructSupport sd;
  TEST_WIRE_TO_JSON_BODY(Struct, sd.GetJSON(), sd.pt);
}

namespace {

test::fidlcat::examples::two_string_struct TwoStringStructFromVals(
    std::string v1, std::string v2) {
  test::fidlcat::examples::two_string_struct tss;
  tss.value1 = v1;
  tss.value2 = v2;
  return tss;
}

}  // namespace

TEST_WIRE_TO_JSON(TwoStringStructInt, TwoStringStructInt,
                  R"({"s":{"value1":"harpo", "value2":"chico"}, "i32":"1"})",
                  TwoStringStructFromVals("harpo", "chico"), 1)

namespace {

std::unique_ptr<test::fidlcat::examples::two_string_struct>
TwoStringStructFromValsPtr(std::string v1, std::string v2) {
  std::unique_ptr<test::fidlcat::examples::two_string_struct> ptr(
      new test::fidlcat::examples::two_string_struct());
  ptr->value1 = v1;
  ptr->value2 = v2;
  return ptr;
}

}  // namespace

TEST_WIRE_TO_JSON(TwoStringNullableStructInt, TwoStringNullableStructInt,
                  R"({"s":{"value1":"harpo", "value2":"chico"}, "i32":"1"})",
                  TwoStringStructFromValsPtr("harpo", "chico"), 1)

TEST_WIRE_TO_JSON(NullableStruct, NullableStruct, R"({"p":null})", nullptr);

TEST_WIRE_TO_JSON(NullableStructAndInt, NullableStructAndInt,
                  R"({"p":null, "i":"1"})", nullptr, 1);

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
  test::fidlcat::examples::two_string_struct tss =
      TwoStringStructFromVals(v1, v2);
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
  test::fidlcat::examples::two_string_struct tss =
      TwoStringStructFromVals(v1, v2);
  ptr->set_variant_tss(tss);
  return ptr;
}

}  // namespace

TEST_WIRE_TO_JSON(UnionInt, Union, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                  GetIntUnion<isu>(42), 1);

TEST_WIRE_TO_JSON(
    UnionStruct, Union,
    R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
    GetStructUnion<isu>("harpo", "chico"), 1);

TEST_WIRE_TO_JSON(NullableUnionInt, NullableUnion,
                  R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                  GetIntUnionPtr<isu>(42), 1);

TEST_WIRE_TO_JSON(
    NullableUnionStruct, NullableUnion,
    R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
    GetStructUnionPtr<isu>("harpo", "chico"), 1);

TEST_WIRE_TO_JSON(NullableUnionIntFirstInt, NullableUnionIntFirst,
                  R"({"i" : "1", "isu":{"variant_i":"42"}})", 1,
                  GetIntUnionPtr<isu>(42));

TEST_WIRE_TO_JSON(
    NullableUnionIntFirstStruct, NullableUnionIntFirst,
    R"({"i": "1", "isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}})",
    1, GetStructUnionPtr<isu>("harpo", "chico"));

TEST_WIRE_TO_JSON(XUnionInt, XUnion, R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                  GetIntUnion<xisu>(42), 1);

TEST_WIRE_TO_JSON(
    XUnionStruct, XUnion,
    R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
    GetStructUnion<xisu>("harpo", "chico"), 1);

TEST_WIRE_TO_JSON(NullableXUnionInt, NullableXUnion,
                  R"({"isu":{"variant_i":"42"}, "i" : "1"})",
                  GetIntUnionPtr<xisu>(42), 1);

TEST_WIRE_TO_JSON(
    NullableXUnionStruct, NullableXUnion,
    R"({"isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}, "i":"1"})",
    GetStructUnionPtr<xisu>("harpo", "chico"), 1);

TEST_WIRE_TO_JSON(NullableXUnionIntFirstInt, NullableXUnionIntFirst,
                  R"({"i" : "1", "isu":{"variant_i":"42"}})", 1,
                  GetIntUnionPtr<xisu>(42));

TEST_WIRE_TO_JSON(
    NullableXUnionIntFirstStruct, NullableXUnionIntFirst,
    R"({"i": "1", "isu":{"variant_tss":{"value1":"harpo","value2":"chico"}}})",
    1, GetStructUnionPtr<xisu>("harpo", "chico"));

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

}  // namespace

TEST_WIRE_TO_JSON(ShortUnion8, ShortUnion,
                  R"({"u":{"variant_u8":"16"}, "i":"1"})",
                  GetUInt8Union<uuu>(16), 1);

TEST_WIRE_TO_JSON(ShortUnion16, ShortUnion,
                  R"({"u":{"variant_u16":"1024"}, "i":"1"})",
                  GetUInt16Union<uuu>(1024), 1);

TEST_WIRE_TO_JSON(ShortXUnion8, ShortXUnion,
                  R"({"u":{"variant_u8":"16"}, "i":"1"})",
                  GetUInt8Union<uux>(16), 1);

TEST_WIRE_TO_JSON(ShortXUnion16, ShortXUnion,
                  R"({"u":{"variant_u16":"1024"}, "i":"1"})",
                  GetUInt16Union<uux>(1024), 1);

// Enum Tests

TEST_WIRE_TO_JSON(DefaultEnum, DefaultEnum, R"({"ev":"x"})",
                  test::fidlcat::examples::default_enum::x);
TEST_WIRE_TO_JSON(I8Enum, I8Enum, R"({"ev":"x"})",
                  test::fidlcat::examples::i8_enum::x);
TEST_WIRE_TO_JSON(I16Enum, I16Enum, R"({"ev":"x"})",
                  test::fidlcat::examples::i16_enum::x);

// Table Tests

test::fidlcat::examples::value_table GetTable(
    std::optional<int16_t> first_int16, std::optional<std::string> value1,
    std::optional<std::string> value2, std::optional<int32_t> third_union_val) {
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

TEST_WIRE_TO_JSON(Table0, Table, R"({"table":{}, "i":"2"})",
                  GetTable({}, {}, {}, {}), 2)

TEST_WIRE_TO_JSON(Table1, Table,
                  R"({"table":{
                          "third_union":{"variant_i":"42"}
                      },
                      "i":"2"})",
                  GetTable({}, {}, {}, 42), 2)

TEST_WIRE_TO_JSON(Table2, Table,
                  R"({"table":{
                          "second_struct":{"value1":"harpo", "value2":"groucho"}
                      },
                      "i":"2"})",
                  GetTable({}, "harpo", "groucho", {}), 2)

TEST_WIRE_TO_JSON(Table3, Table,
                  R"({"table":{
                          "second_struct":{
                              "value1":"harpo", "value2":"groucho"},
                          "third_union":{"variant_i":"42"}},
                      "i":"2"})",
                  GetTable({}, "harpo", "groucho", 42), 2)

TEST_WIRE_TO_JSON(Table4, Table, R"({"table":{
                                         "first_int16":"1"
                                     },
                                     "i":"2"})",
                  GetTable(1, {}, {}, {}), 2)

TEST_WIRE_TO_JSON(Table5, Table,
                  R"({"table":{
                          "first_int16":"1",
                          "third_union":{"variant_i":"42"}
                      },
                      "i":"2"})",
                  GetTable(1, {}, {}, 42), 2)

TEST_WIRE_TO_JSON(Table6, Table,
                  R"({"table":{
                          "first_int16":"1",
                          "second_struct":{
                              "value1":"harpo", "value2":"groucho"}
                      },
                      "i":"2"})",
                  GetTable(1, "harpo", "groucho", {}), 2)

TEST_WIRE_TO_JSON(Table7, Table,
                  R"({"table":{
                          "first_int16":"1",
                          "second_struct":{
                              "value1":"harpo", "value2":"groucho"},
                          "third_union":{"variant_i":"42"}
                      },
                      "i":"2"})",
                  GetTable(1, "harpo", "groucho", 42), 2)

// TODO(DX-1476): Add a test that exercises what happens when we encounter an
// unknown type in a table.

// Handle Tests

namespace {

class HandleSupport {
 public:
  HandleSupport() {
    zx::channel::create(0, &out1_, &out2_);
    json_ = R"({"ch":")";
    json_.append(std::to_string(out2_.get()));
    json_.append(R"("})");
  }
  zx::channel handle() { return std::move(out2_); }
  std::string GetJSON() { return json_; }

 private:
  zx::channel out1_;
  zx::channel out2_;
  std::string json_;
};

}  // namespace

TEST_F(WireParserTest, ParseHandle) {
  HandleSupport support;
  TEST_WIRE_TO_JSON_BODY(Handle, support.GetJSON(), support.handle());
}

TEST_F(WireParserTest, ParseNullableHandle) {
  HandleSupport support;
  TEST_WIRE_TO_JSON_BODY(NullableHandle, support.GetJSON(), support.handle());
}

namespace {

class HandleStructSupport {
 public:
  HandleStructSupport() {
    zx::channel::create(0, &out1_, &out2_);
    zx::channel::create(0, &out3_, &out4_);
    json_ = R"({"hs":{"h1":")";
    json_.append(std::to_string(out1_.get()));
    json_.append(R"(", "h2":")");
    json_.append(std::to_string(out2_.get()));
    json_.append(R"(", "h3":")");
    json_.append(std::to_string(out3_.get()));
    json_.append(R"("}})");
  }
  test::fidlcat::examples::handle_struct handle_struct() {
    test::fidlcat::examples::handle_struct hs;
    hs.h1 = std::move(out1_);
    hs.h2 = std::move(out2_);
    hs.h3 = std::move(out3_);
    return hs;
  }
  std::string GetJSON() { return json_; }

 private:
  zx::channel out1_;
  zx::channel out2_;
  zx::channel out3_;
  zx::channel out4_;

  std::string json_;
};

}  // namespace

TEST_F(WireParserTest, ParseHandleStruct) {
  HandleStructSupport support;
  TEST_WIRE_TO_JSON_BODY(HandleStruct, support.GetJSON(),
                         support.handle_struct());
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
      "name": "test.fidlcat.examples/this_is_an_interface",
      "location": {
        "filename": "../../tools/fidlcat/lib/testdata/types.test.fidl",
        "line": 7,
        "column": 9
      },
      "methods": [
        {
          "ordinal": 912304001,
          "generated_ordinal": 912304001,
          "name": "Int32",
          "location": {
            "filename": "../../tools/fidlcat/lib/testdata/types.test.fidl",
            "line": 12,
            "column": 4
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
                "line": 12,
                "column": 16
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
          "has_response": false
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

  InterceptRequest<test::fidlcat::examples::this_is_an_interface>(
      message,
      [](fidl::InterfacePtr<test::fidlcat::examples::this_is_an_interface>&
             ptr) { ptr->Int32(0xdeadbeef); });

  fidl_message_header_t header = message.header();

  const InterfaceMethod* method;
  // If this is false, you probably have to update the schema above.
  ASSERT_TRUE(loader.GetByOrdinal(header.ordinal, &method));

  rapidjson::Document actual;
  fidlcat::RequestToJSON(method, message, actual);

  ASSERT_STREQ(actual["i32"].GetString(), "ef be ad de");
}

}  // namespace fidlcat
