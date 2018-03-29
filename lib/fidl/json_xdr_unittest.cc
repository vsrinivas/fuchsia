// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <vector>

#include <fuchsia/cpp/json_xdr_unittest.h>
#include "gtest/gtest.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {
namespace {

struct T {
  int i;
  std::string s;
  bool b;

  std::vector<int> vi;
  std::vector<std::string> vs;
  std::vector<bool> vb;

  std::map<int, int> mi;
  std::map<std::string, int> ms;
  std::map<bool, bool> mb;
};

void XdrT(XdrContext* const xdr, T* const data) {
  xdr->Field("i", &data->i);
  xdr->Field("s", &data->s);
  xdr->Field("b", &data->b);
  xdr->Field("vi", &data->vi);
  xdr->Field("vs", &data->vs);
  xdr->Field("vb", &data->vb);
  xdr->Field("mi", &data->mi);
  xdr->Field("ms", &data->ms);
  xdr->Field("mb", &data->mb);
}

TEST(Xdr, Struct) {
  std::string json;

  T t0;
  t0.i = 1;
  t0.s = "2";
  t0.b = true;
  t0.vi.push_back(3);
  t0.vs.push_back("4");
  t0.vb.push_back(true);
  t0.mi[5] = 6;
  t0.ms["7"] = 8;
  t0.mb[true] = false;
  XdrWrite(&json, &t0, XdrT);

  T t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrT));

  EXPECT_EQ(t1.i, t0.i);
  EXPECT_EQ(t1.s, t0.s);
  EXPECT_EQ(t1.b, t0.b);
  EXPECT_EQ(t1.vi, t0.vi);
  EXPECT_EQ(t1.vs, t0.vs);
  EXPECT_EQ(t1.vb, t0.vb);
  EXPECT_EQ(t1.mi, t0.mi);
  EXPECT_EQ(t1.ms, t0.ms);
  EXPECT_EQ(t1.mb, t0.mb);
}

void XdrStruct(XdrContext* const xdr, json_xdr_unittest::Struct* const data) {
  xdr->Field("item", &data->item);
}

void XdrUnion(XdrContext* const xdr, json_xdr_unittest::Union* const data) {
  // NOTE(mesch): There is no direct support for FIDL unions in XdrContext,
  // mostly because we cannot point a union field in the same way s we can point
  // to a struct field.
  //
  // The below is the current best way we have figured out to XDR unions. A
  // lager and more realistic (and slightly different) real life example of
  // XDRing a FIDL union type is XdrNoun() in story_controller_impl.cc.

  static constexpr char kTag[] = "@tag";
  static constexpr char kValue[] = "@value";
  static constexpr char kString[] = "string";
  static constexpr char kInt32[] = "int32";

  switch (xdr->op()) {
    case XdrOp::FROM_JSON: {
      std::string tag;
      xdr->Field(kTag, &tag);

      if (tag == kString) {
        std::string value;
        xdr->Field(kValue, &value);
        data->set_string(std::move(value));

      } else if (tag == kInt32) {
        int32_t value;
        xdr->Field(kValue, &value);
        data->set_int32(std::move(value));

      } else {
        ASSERT_TRUE(false) << "XdrUnion FROM_JSON unknown tag: " << tag;
      }
      break;
    }

    case XdrOp::TO_JSON: {
      std::string tag;

      switch (data->Which()) {
        case json_xdr_unittest::Union::Tag::kString: {
          tag = kString;
          std::string value = data->string();
          xdr->Field(kValue, &value);
          break;
        }
        case json_xdr_unittest::Union::Tag::kInt32: {
          tag = kInt32;
          int32_t value = data->int32();
          xdr->Field(kValue, &value);
          break;
        }
        case json_xdr_unittest::Union::Tag::Invalid:
          ASSERT_TRUE(false) << "XdrUnion TO_JSON unknown tag: "
            << static_cast<int>(data->Which());
          break;
      }

      xdr->Field(kTag, &tag);
      break;
    }
  }
}

// Data can be any of RequiredData, RequiredRepeatedRequiredData,
// OptionalRepeatedRequiredData.
template <typename Data>
void XdrRequiredData(XdrContext* const xdr, Data* const data) {
  xdr->Field("string", &data->string);
  xdr->Field("bool", &data->bool_);
  xdr->Field("int8", &data->int8);
  xdr->Field("int16", &data->int16);
  xdr->Field("int32", &data->int32);
  xdr->Field("int64", &data->int64);
  xdr->Field("uint8", &data->uint8);
  xdr->Field("uint16", &data->uint16);
  xdr->Field("uint32", &data->uint32);
  xdr->Field("uint64", &data->uint64);
  xdr->Field("float32", &data->float32);
  xdr->Field("float64", &data->float64);
  xdr->Field("struct", &data->struct_, XdrStruct);
  xdr->Field("enum", &data->enum_);
  xdr->Field("union", &data->union_, XdrUnion);
}

// Data can be any of OptionalData, RequiredRepeatedOptionalData,
// OptionalRepeatedOptionalData.
template <typename Data>
void XdrOptionalData(XdrContext* const xdr, Data* const data) {
  xdr->Field("string", &data->string);
  xdr->Field("struct", &data->struct_, XdrStruct);
  xdr->Field("union", &data->union_, XdrUnion);
}

TEST(Xdr, FidlRequired) {
  std::string json;

  json_xdr_unittest::RequiredData t0;

  t0.string = "1";
  t0.bool_ = true;
  t0.int8 = 2;
  t0.int16 = 3;
  t0.int32 = 4;
  t0.int64 = 5;
  t0.uint8 = 6;
  t0.uint16 = 7;
  t0.uint32 = 8;
  t0.uint64 = 9;
  t0.float32 = 10;
  t0.float64 = 11;
  t0.struct_.item = 12;
  t0.enum_ = json_xdr_unittest::Enum::ONE;
  t0.union_.set_int32(13);

  XdrWrite(&json, &t0, XdrRequiredData<json_xdr_unittest::RequiredData>);

  json_xdr_unittest::RequiredData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrRequiredData<json_xdr_unittest::RequiredData>));

  EXPECT_EQ(t1, t0) << json;

  // Technically not needed because the equality should cover this, but makes it
  // more transparent what's going on.
  EXPECT_EQ("1", t1.string);
  EXPECT_TRUE(t1.bool_);
  EXPECT_EQ(2, t1.int8);
  EXPECT_EQ(3, t1.int16);
  EXPECT_EQ(4, t1.int32);
  EXPECT_EQ(5, t1.int64);
  EXPECT_EQ(6u, t1.uint8);
  EXPECT_EQ(7u, t1.uint16);
  EXPECT_EQ(8u, t1.uint32);
  EXPECT_EQ(9u, t1.uint64);
  EXPECT_EQ(10.0f, t1.float32);
  EXPECT_EQ(11.0, t1.float64);
  EXPECT_EQ(12, t1.struct_.item);
  EXPECT_EQ(json_xdr_unittest::Enum::ONE, t1.enum_);
  EXPECT_TRUE(t1.union_.is_int32());
  EXPECT_EQ(13, t1.union_.int32());
}

TEST(Xdr, FidlOptional) {
  std::string json;

  json_xdr_unittest::OptionalData t0;

  t0.string = "1";
  t0.struct_ = json_xdr_unittest::Struct::New();
  t0.struct_->item = 12;
  t0.union_ = json_xdr_unittest::Union::New();
  t0.union_->set_int32(13);

  XdrWrite(&json, &t0, XdrOptionalData<json_xdr_unittest::OptionalData>);

  json_xdr_unittest::OptionalData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrOptionalData<json_xdr_unittest::OptionalData>));

  EXPECT_EQ(t1, t0) << json;

  // See comment in FidlRequired.
  EXPECT_FALSE(t1.string.is_null());
  EXPECT_EQ("1", t1.string);

  EXPECT_FALSE(nullptr == t1.struct_);
  EXPECT_EQ(12, t1.struct_->item);

  EXPECT_FALSE(nullptr == t1.union_);
  EXPECT_TRUE(t1.union_->is_int32());
  EXPECT_EQ(13, t1.union_->int32());

  t1.string.reset();
  t1.struct_.reset();
  t1.union_.reset();

  XdrWrite(&json, &t1, XdrOptionalData<json_xdr_unittest::OptionalData>);

  json_xdr_unittest::OptionalData t2;
  EXPECT_TRUE(XdrRead(json, &t2, XdrOptionalData<json_xdr_unittest::OptionalData>));

  // Always fails, FIDL-129.
  //EXPECT_EQ(t2, t1) << json;

  // See comment in FidlRequired.
  EXPECT_TRUE(t2.string.is_null());
  EXPECT_TRUE(nullptr == t2.struct_);
  EXPECT_TRUE(nullptr == t2.union_);
}

TEST(Xdr, FidlRequiredRepeatedRequired) {
  std::string json;

  json_xdr_unittest::RequiredRepeatedRequiredData t0;

  t0.string.push_back("1");
  t0.bool_.push_back(true);
  t0.int8.push_back(2);
  t0.int16.push_back(3);
  t0.int32.push_back(4);
  t0.int64.push_back(5);
  t0.uint8.push_back(6);
  t0.uint16.push_back(7);
  t0.uint32.push_back(8);
  t0.uint64.push_back(9);
  t0.float32.push_back(10);
  t0.float64.push_back(11);
  t0.struct_.push_back({12});
  t0.enum_.push_back(json_xdr_unittest::Enum::ONE);

  json_xdr_unittest::Union u;
  u.set_int32(13);
  t0.union_.push_back(std::move(u));

  XdrWrite(&json, &t0, XdrRequiredData<json_xdr_unittest::RequiredRepeatedRequiredData>);

  json_xdr_unittest::RequiredRepeatedRequiredData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrRequiredData<json_xdr_unittest::RequiredRepeatedRequiredData>));

  EXPECT_EQ(t1, t0) << json;

  EXPECT_EQ(1u, t1.string->size());
  EXPECT_EQ(1u, t1.bool_->size());
  EXPECT_EQ(1u, t1.int8->size());
  EXPECT_EQ(1u, t1.int16->size());
  EXPECT_EQ(1u, t1.int32->size());
  EXPECT_EQ(1u, t1.int64->size());
  EXPECT_EQ(1u, t1.uint8->size());
  EXPECT_EQ(1u, t1.uint16->size());
  EXPECT_EQ(1u, t1.uint32->size());
  EXPECT_EQ(1u, t1.uint64->size());
  EXPECT_EQ(1u, t1.float32->size());
  EXPECT_EQ(1u, t1.float64->size());
  EXPECT_EQ(1u, t1.struct_->size());
  EXPECT_EQ(1u, t1.enum_->size());
  EXPECT_EQ(1u, t1.union_->size());

  EXPECT_EQ("1", t1.string->at(0));
  EXPECT_TRUE(t1.bool_->at(0));
  EXPECT_EQ(2, t1.int8->at(0));
  EXPECT_EQ(3, t1.int16->at(0));
  EXPECT_EQ(4, t1.int32->at(0));
  EXPECT_EQ(5, t1.int64->at(0));
  EXPECT_EQ(6u, t1.uint8->at(0));
  EXPECT_EQ(7u, t1.uint16->at(0));
  EXPECT_EQ(8u, t1.uint32->at(0));
  EXPECT_EQ(9u, t1.uint64->at(0));
  EXPECT_EQ(10.0f, t1.float32->at(0));
  EXPECT_EQ(11.0, t1.float64->at(0));
  EXPECT_EQ(12, t1.struct_->at(0).item);
  EXPECT_EQ(json_xdr_unittest::Enum::ONE, t1.enum_->at(0));
  EXPECT_TRUE(t1.union_->at(0).is_int32());
  EXPECT_EQ(13, t1.union_->at(0).int32());
}

TEST(Xdr, FidlRequiredRepeatedOptional) {
  std::string json;

  json_xdr_unittest::RequiredRepeatedOptionalData t0;
  t0.string.push_back("1");

  json_xdr_unittest::StructPtr s = json_xdr_unittest::Struct::New();
  s->item = 12;
  t0.struct_.push_back(std::move(s));

  json_xdr_unittest::UnionPtr u = json_xdr_unittest::Union::New();
  u->set_int32(13);
  t0.union_.push_back(std::move(u));

  XdrWrite(&json, &t0, XdrOptionalData<json_xdr_unittest::RequiredRepeatedOptionalData>);

  json_xdr_unittest::RequiredRepeatedOptionalData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrOptionalData<json_xdr_unittest::RequiredRepeatedOptionalData>));

  // Always fails, FIDL-128.
  //EXPECT_EQ(t1, t0) << json;

  // See comment in FidlRequired.
  EXPECT_EQ(1u, t1.string->size());
  EXPECT_EQ(1u, t1.struct_->size());
  EXPECT_EQ(1u, t1.union_->size());

  EXPECT_FALSE(t1.string->at(0).is_null());
  EXPECT_EQ("1", t1.string->at(0));

  EXPECT_FALSE(nullptr == t1.struct_->at(0));
  EXPECT_EQ(12, t1.struct_->at(0)->item);

  EXPECT_FALSE(nullptr == t1.union_->at(0));
  EXPECT_TRUE(t1.union_->at(0)->is_int32());
  EXPECT_EQ(13, t1.union_->at(0)->int32());

  t1.string->at(0).reset();
  t1.struct_->at(0).reset();
  t1.union_->at(0).reset();

  XdrWrite(&json, &t1, XdrOptionalData<json_xdr_unittest::RequiredRepeatedOptionalData>);

  json_xdr_unittest::RequiredRepeatedOptionalData t2;
  EXPECT_TRUE(XdrRead(json, &t2, XdrOptionalData<json_xdr_unittest::RequiredRepeatedOptionalData>));

  EXPECT_EQ(t2, t1) << json;

  // See comment in FidlRequired.
  EXPECT_EQ(1u, t2.string->size());
  EXPECT_EQ(1u, t2.struct_->size());
  EXPECT_EQ(1u, t2.union_->size());

  EXPECT_TRUE(t2.string->at(0).is_null());
  EXPECT_TRUE(nullptr == t2.struct_->at(0));
  EXPECT_TRUE(nullptr == t2.union_->at(0));
}

TEST(Xdr, FidlOptionalRepeatedRequired) {
  std::string json;

  json_xdr_unittest::OptionalRepeatedRequiredData t0;

  t0.string.push_back("1");
  t0.bool_.push_back(true);
  t0.int8.push_back(2);
  t0.int16.push_back(3);
  t0.int32.push_back(4);
  t0.int64.push_back(5);
  t0.uint8.push_back(6);
  t0.uint16.push_back(7);
  t0.uint32.push_back(8);
  t0.uint64.push_back(9);
  t0.float32.push_back(10);
  t0.float64.push_back(11);
  t0.struct_.push_back({12});
  t0.enum_.push_back(json_xdr_unittest::Enum::ONE);
  json_xdr_unittest::Union u;
  u.set_int32(13);
  t0.union_.push_back(std::move(u));

  XdrWrite(&json, &t0, XdrRequiredData<json_xdr_unittest::OptionalRepeatedRequiredData>);

  json_xdr_unittest::OptionalRepeatedRequiredData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrRequiredData<json_xdr_unittest::OptionalRepeatedRequiredData>));

  EXPECT_EQ(t1, t0) << json;

  EXPECT_FALSE(t1.string.is_null());
  EXPECT_FALSE(t1.bool_.is_null());
  EXPECT_FALSE(t1.int8.is_null());
  EXPECT_FALSE(t1.int16.is_null());
  EXPECT_FALSE(t1.int32.is_null());
  EXPECT_FALSE(t1.int64.is_null());
  EXPECT_FALSE(t1.uint8.is_null());
  EXPECT_FALSE(t1.uint16.is_null());
  EXPECT_FALSE(t1.uint32.is_null());
  EXPECT_FALSE(t1.uint64.is_null());
  EXPECT_FALSE(t1.float32.is_null());
  EXPECT_FALSE(t1.float64.is_null());
  EXPECT_FALSE(t1.struct_.is_null());
  EXPECT_FALSE(t1.enum_.is_null());
  EXPECT_FALSE(t1.union_.is_null());

  EXPECT_EQ(1u, t1.string->size());
  EXPECT_EQ(1u, t1.bool_->size());
  EXPECT_EQ(1u, t1.int8->size());
  EXPECT_EQ(1u, t1.int16->size());
  EXPECT_EQ(1u, t1.int32->size());
  EXPECT_EQ(1u, t1.int64->size());
  EXPECT_EQ(1u, t1.uint8->size());
  EXPECT_EQ(1u, t1.uint16->size());
  EXPECT_EQ(1u, t1.uint32->size());
  EXPECT_EQ(1u, t1.uint64->size());
  EXPECT_EQ(1u, t1.float32->size());
  EXPECT_EQ(1u, t1.float64->size());
  EXPECT_EQ(1u, t1.struct_->size());
  EXPECT_EQ(1u, t1.enum_->size());
  EXPECT_EQ(1u, t1.union_->size());

  EXPECT_EQ("1", t1.string->at(0));
  EXPECT_TRUE(t1.bool_->at(0));
  EXPECT_EQ(2, t1.int8->at(0));
  EXPECT_EQ(3, t1.int16->at(0));
  EXPECT_EQ(4, t1.int32->at(0));
  EXPECT_EQ(5, t1.int64->at(0));
  EXPECT_EQ(6u, t1.uint8->at(0));
  EXPECT_EQ(7u, t1.uint16->at(0));
  EXPECT_EQ(8u, t1.uint32->at(0));
  EXPECT_EQ(9u, t1.uint64->at(0));
  EXPECT_EQ(10.0f, t1.float32->at(0));
  EXPECT_EQ(11.0, t1.float64->at(0));
  EXPECT_EQ(12, t1.struct_->at(0).item);
  EXPECT_EQ(json_xdr_unittest::Enum::ONE, t1.enum_->at(0));
  EXPECT_TRUE(t1.union_->at(0).is_int32());
  EXPECT_EQ(13, t1.union_->at(0).int32());

  t1.string.reset();
  t1.bool_.reset();
  t1.int8.reset();
  t1.int16.reset();
  t1.int32.reset();
  t1.int64.reset();
  t1.uint8.reset();
  t1.uint16.reset();
  t1.uint32.reset();
  t1.uint64.reset();
  t1.float32.reset();
  t1.float64.reset();
  t1.struct_.reset();
  t1.enum_.reset();
  t1.union_.reset();

  XdrWrite(&json, &t1, XdrRequiredData<json_xdr_unittest::OptionalRepeatedRequiredData>);

  json_xdr_unittest::OptionalRepeatedRequiredData t2;
  EXPECT_TRUE(XdrRead(json, &t2, XdrRequiredData<json_xdr_unittest::OptionalRepeatedRequiredData>));

  EXPECT_EQ(t2, t1) << json;

  EXPECT_TRUE(t2.string.is_null());
  EXPECT_TRUE(t2.bool_.is_null());
  EXPECT_TRUE(t2.int8.is_null());
  EXPECT_TRUE(t2.int16.is_null());
  EXPECT_TRUE(t2.int32.is_null());
  EXPECT_TRUE(t2.int64.is_null());
  EXPECT_TRUE(t2.uint8.is_null());
  EXPECT_TRUE(t2.uint16.is_null());
  EXPECT_TRUE(t2.uint32.is_null());
  EXPECT_TRUE(t2.uint64.is_null());
  EXPECT_TRUE(t2.float32.is_null());
  EXPECT_TRUE(t2.float64.is_null());
  EXPECT_TRUE(t2.struct_.is_null());
  EXPECT_TRUE(t2.enum_.is_null());
  EXPECT_TRUE(t2.union_.is_null());

  EXPECT_EQ(0u, t2.string->size());
  EXPECT_EQ(0u, t2.bool_->size());
  EXPECT_EQ(0u, t2.int8->size());
  EXPECT_EQ(0u, t2.int16->size());
  EXPECT_EQ(0u, t2.int32->size());
  EXPECT_EQ(0u, t2.int64->size());
  EXPECT_EQ(0u, t2.uint8->size());
  EXPECT_EQ(0u, t2.uint16->size());
  EXPECT_EQ(0u, t2.uint32->size());
  EXPECT_EQ(0u, t2.uint64->size());
  EXPECT_EQ(0u, t2.float32->size());
  EXPECT_EQ(0u, t2.float64->size());
  EXPECT_EQ(0u, t2.struct_->size());
  EXPECT_EQ(0u, t2.enum_->size());
  EXPECT_EQ(0u, t2.union_->size());
}

TEST(Xdr, FidlOptionalRepeatedOptional) {
  std::string json;

  json_xdr_unittest::OptionalRepeatedOptionalData t0;
  t0.string.push_back("1");

  json_xdr_unittest::StructPtr s = json_xdr_unittest::Struct::New();
  s->item = 12;
  t0.struct_.push_back(std::move(s));

  json_xdr_unittest::UnionPtr u = json_xdr_unittest::Union::New();
  u->set_int32(13);
  t0.union_.push_back(std::move(u));

  XdrWrite(&json, &t0, XdrOptionalData<json_xdr_unittest::OptionalRepeatedOptionalData>);

  json_xdr_unittest::OptionalRepeatedOptionalData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrOptionalData<json_xdr_unittest::OptionalRepeatedOptionalData>));

  // Always fails, FIDL-128.
  //EXPECT_EQ(t1, t0) << json;

  // See comment in FidlRequired.
  EXPECT_FALSE(t1.string.is_null());
  EXPECT_FALSE(t1.struct_.is_null());
  EXPECT_FALSE(t1.union_.is_null());

  EXPECT_EQ(1u, t1.string->size());
  EXPECT_EQ(1u, t1.struct_->size());
  EXPECT_EQ(1u, t1.union_->size());

  EXPECT_FALSE(t1.string->at(0).is_null());
  EXPECT_EQ("1", t1.string->at(0));

  EXPECT_FALSE(nullptr == t1.struct_->at(0));
  EXPECT_EQ(12, t1.struct_->at(0)->item);

  EXPECT_FALSE(nullptr == t1.union_->at(0));
  EXPECT_TRUE(t1.union_->at(0)->is_int32());
  EXPECT_EQ(13, t1.union_->at(0)->int32());

  t1.string->at(0).reset();
  t1.struct_->at(0).reset();
  t1.union_->at(0).reset();

  XdrWrite(&json, &t1, XdrOptionalData<json_xdr_unittest::OptionalRepeatedOptionalData>);

  json_xdr_unittest::OptionalRepeatedOptionalData t2;
  EXPECT_TRUE(XdrRead(json, &t2, XdrOptionalData<json_xdr_unittest::OptionalRepeatedOptionalData>));

  EXPECT_EQ(t2, t1) << json;

  // See comment in FidlRequired.
  EXPECT_FALSE(t2.string.is_null());
  EXPECT_FALSE(t2.struct_.is_null());
  EXPECT_FALSE(t2.union_.is_null());

  EXPECT_EQ(1u, t2.string->size());
  EXPECT_EQ(1u, t2.struct_->size());
  EXPECT_EQ(1u, t2.union_->size());

  EXPECT_TRUE(t2.string->at(0).is_null());
  EXPECT_TRUE(nullptr == t2.struct_->at(0));
  EXPECT_TRUE(nullptr == t2.union_->at(0));

  t2.string.reset();
  t2.struct_.reset();
  t2.union_.reset();

  XdrWrite(&json, &t2, XdrOptionalData<json_xdr_unittest::OptionalRepeatedOptionalData>);

  json_xdr_unittest::OptionalRepeatedOptionalData t3;
  EXPECT_TRUE(XdrRead(json, &t3, XdrOptionalData<json_xdr_unittest::OptionalRepeatedOptionalData>));

  EXPECT_EQ(t3, t2) << json;

  // See comment in FidlRequired.
  EXPECT_TRUE(t3.string.is_null());
  EXPECT_TRUE(t3.struct_.is_null());
  EXPECT_TRUE(t3.union_.is_null());

  EXPECT_EQ(0u, t3.string->size());
  EXPECT_EQ(0u, t3.struct_->size());
  EXPECT_EQ(0u, t3.union_->size());
}

}  // namespace
}  // namespace modular
