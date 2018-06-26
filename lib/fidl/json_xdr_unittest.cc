// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/json_xdr.h"

#include <map>
#include <vector>

#include <test/peridot/lib/fidl/jsonxdr/cpp/fidl.h>

#include "gtest/gtest.h"

namespace modular {
namespace {

namespace json_xdr_unittest = ::test::peridot::lib::fidl::jsonxdr;

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

void XdrT_v1(XdrContext* const xdr, T* const data) {
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

constexpr XdrFilterType<T> XdrT[] = {
    XdrT_v1,
    nullptr,
};

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

void XdrT_v2(XdrContext* const xdr, T* const data) {
  xdr->Field("i", &data->i);
  xdr->Field("s", &data->s);
  xdr->Field("b", &data->b);
  xdr->Field("vi", &data->vi);
  xdr->Field("vs_v2", &data->vs);
  xdr->Field("vb", &data->vb);
  xdr->Field("mi", &data->mi);
  xdr->Field("ms", &data->ms);
  xdr->Field("mb", &data->mb);
}

TEST(Xdr, StructVersions) {
  // Filter versioning with version lists: Write with an old version of the
  // filter, then attempt to read with a newer version only, which fails.
  // Attempt again with a filter verision list that has both the old and the new
  // version of filter, which succeeds.

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

  constexpr XdrFilterType<T> filter_versions_v2_only[] = {
      XdrT_v2,
      nullptr,
  };

  EXPECT_FALSE(XdrRead(json, &t1, filter_versions_v2_only));

  constexpr XdrFilterType<T> filter_versions_all[] = {
      XdrT_v2,
      XdrT_v1,
      nullptr,
  };

  EXPECT_TRUE(XdrRead(json, &t1, filter_versions_all));
}

void XdrT_v3(XdrContext* const xdr, T* const data) {
  if (!xdr->Version(3)) {
    return;
  }

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

TEST(Xdr, StructVersionsExplicitFallback) {
  // Filter versioning with explicit version numbers: Write with an old version
  // of the filter without version number, then attempt to read with a filter
  // that expects a version number, which fails. Attempt again with a filter
  // version list that has both the old and the new version of filter, which
  // succeeds.

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

  constexpr XdrFilterType<T> filter_versions_v3_only[] = {
      XdrT_v3,
      nullptr,
  };

  EXPECT_FALSE(XdrRead(json, &t1, filter_versions_v3_only));

  constexpr XdrFilterType<T> filter_versions_all[] = {
      XdrT_v3,
      XdrT_v1,
      nullptr,
  };

  EXPECT_TRUE(XdrRead(json, &t1, filter_versions_all));
}

void XdrT_v4(XdrContext* const xdr, T* const data) {
  if (!xdr->Version(4)) {
    return;
  }

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

TEST(Xdr, StructVersionsExplicit) {
  // Filter versioning with explicit version numbers: Write with a version of
  // the filter with a version number, then attempt to read it back with the
  // same filter, which succeeds. Attempt to read it with a newer version
  // filter, which fails.

  std::string json;

  constexpr XdrFilterType<T> filter_versions_v3_only[] = {
      XdrT_v3,
      nullptr,
  };

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
  XdrWrite(&json, &t0, filter_versions_v3_only);

  T t1;
  EXPECT_TRUE(XdrRead(json, &t1, filter_versions_v3_only));

  constexpr XdrFilterType<T> filter_versions_v4_only[] = {
      XdrT_v4,
      nullptr,
  };

  T t2;
  EXPECT_FALSE(XdrRead(json, &t2, filter_versions_v4_only));

  constexpr XdrFilterType<T> filter_versions_all[] = {
      XdrT_v4, XdrT_v3, XdrT_v2, XdrT_v1, nullptr,
  };

  T t3;
  EXPECT_TRUE(XdrRead(json, &t3, filter_versions_all));
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
void XdrRequiredData_v1(XdrContext* const xdr, Data* const data) {
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
void XdrOptionalData_v1(XdrContext* const xdr, Data* const data) {
  xdr->Field("string", &data->string);
  xdr->Field("struct", &data->struct_, XdrStruct);
  xdr->Field("union", &data->union_, XdrUnion);
}

constexpr XdrFilterType<json_xdr_unittest::RequiredData> XdrRequiredData[] = {
    XdrRequiredData_v1<json_xdr_unittest::RequiredData>,
    nullptr,
};

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

  XdrWrite(&json, &t0, XdrRequiredData);

  json_xdr_unittest::RequiredData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrRequiredData));

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

constexpr XdrFilterType<json_xdr_unittest::OptionalData> XdrOptionalData[] = {
    XdrOptionalData_v1<json_xdr_unittest::OptionalData>,
    nullptr,
};

TEST(Xdr, FidlOptional) {
  std::string json;

  json_xdr_unittest::OptionalData t0;

  t0.string = "1";
  t0.struct_ = json_xdr_unittest::Struct::New();
  t0.struct_->item = 12;
  t0.union_ = json_xdr_unittest::Union::New();
  t0.union_->set_int32(13);

  XdrWrite(&json, &t0, XdrOptionalData);

  json_xdr_unittest::OptionalData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrOptionalData));

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

  XdrWrite(&json, &t1, XdrOptionalData);

  json_xdr_unittest::OptionalData t2;
  EXPECT_TRUE(XdrRead(json, &t2, XdrOptionalData));

  EXPECT_EQ(t2, t1) << json;

  // See comment in FidlRequired.
  EXPECT_TRUE(t2.string.is_null());
  EXPECT_TRUE(nullptr == t2.struct_);
  EXPECT_TRUE(nullptr == t2.union_);
}

constexpr XdrFilterType<json_xdr_unittest::RequiredRepeatedRequiredData>
    XdrRequiredRepeatedRequiredData[] = {
        XdrRequiredData_v1<json_xdr_unittest::RequiredRepeatedRequiredData>,
        nullptr,
};

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

  XdrWrite(&json, &t0, XdrRequiredRepeatedRequiredData);

  json_xdr_unittest::RequiredRepeatedRequiredData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrRequiredRepeatedRequiredData));

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

constexpr XdrFilterType<json_xdr_unittest::RequiredRepeatedOptionalData>
    XdrRequiredRepeatedOptionalData[] = {
        XdrOptionalData_v1<json_xdr_unittest::RequiredRepeatedOptionalData>,
        nullptr,
};

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

  XdrWrite(&json, &t0, XdrRequiredRepeatedOptionalData);

  json_xdr_unittest::RequiredRepeatedOptionalData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrRequiredRepeatedOptionalData));

  EXPECT_EQ(t1, t0) << json;

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

  XdrWrite(&json, &t1, XdrRequiredRepeatedOptionalData);

  json_xdr_unittest::RequiredRepeatedOptionalData t2;
  EXPECT_TRUE(XdrRead(json, &t2, XdrRequiredRepeatedOptionalData));

  EXPECT_EQ(t2, t1) << json;

  // See comment in FidlRequired.
  EXPECT_EQ(1u, t2.string->size());
  EXPECT_EQ(1u, t2.struct_->size());
  EXPECT_EQ(1u, t2.union_->size());

  EXPECT_TRUE(t2.string->at(0).is_null());
  EXPECT_TRUE(nullptr == t2.struct_->at(0));
  EXPECT_TRUE(nullptr == t2.union_->at(0));
}

constexpr XdrFilterType<json_xdr_unittest::OptionalRepeatedRequiredData>
    XdrOptionalRepeatedRequiredData[] = {
        XdrRequiredData_v1<json_xdr_unittest::OptionalRepeatedRequiredData>,
        nullptr,
};

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

  XdrWrite(&json, &t0, XdrOptionalRepeatedRequiredData);

  json_xdr_unittest::OptionalRepeatedRequiredData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrOptionalRepeatedRequiredData));

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

  XdrWrite(&json, &t1, XdrOptionalRepeatedRequiredData);

  json_xdr_unittest::OptionalRepeatedRequiredData t2;
  EXPECT_TRUE(XdrRead(json, &t2, XdrOptionalRepeatedRequiredData));

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

constexpr XdrFilterType<json_xdr_unittest::OptionalRepeatedOptionalData>
    XdrOptionalRepeatedOptionalData[] = {
        XdrOptionalData_v1<json_xdr_unittest::OptionalRepeatedOptionalData>,
        nullptr,
};

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

  XdrWrite(&json, &t0, XdrOptionalRepeatedOptionalData);

  json_xdr_unittest::OptionalRepeatedOptionalData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrOptionalRepeatedOptionalData));

  EXPECT_EQ(t1, t0) << json;

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

  XdrWrite(&json, &t1, XdrOptionalRepeatedOptionalData);

  json_xdr_unittest::OptionalRepeatedOptionalData t2;
  EXPECT_TRUE(XdrRead(json, &t2, XdrOptionalRepeatedOptionalData));

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

  XdrWrite(&json, &t2, XdrOptionalRepeatedOptionalData);

  json_xdr_unittest::OptionalRepeatedOptionalData t3;
  EXPECT_TRUE(XdrRead(json, &t3, XdrOptionalRepeatedOptionalData));

  EXPECT_EQ(t3, t2) << json;

  // See comment in FidlRequired.
  EXPECT_TRUE(t3.string.is_null());
  EXPECT_TRUE(t3.struct_.is_null());
  EXPECT_TRUE(t3.union_.is_null());

  EXPECT_EQ(0u, t3.string->size());
  EXPECT_EQ(0u, t3.struct_->size());
  EXPECT_EQ(0u, t3.union_->size());
}

constexpr XdrFilterType<json_xdr_unittest::ArrayData> XdrArrayData[] = {
    XdrRequiredData_v1<json_xdr_unittest::ArrayData>,
    nullptr,
};

TEST(Xdr, FidlArray) {
  std::string json;

  json_xdr_unittest::ArrayData t0;

  t0.string.at(0) = "1";
  t0.bool_.at(0) = true;
  t0.int8.at(0) = 2;
  t0.int16.at(0) = 3;
  t0.int32.at(0) = 4;
  t0.int64.at(0) = 5;
  t0.uint8.at(0) = 6;
  t0.uint16.at(0) = 7;
  t0.uint32.at(0) = 8;
  t0.uint64.at(0) = 9;
  t0.float32.at(0) = 10;
  t0.float64.at(0) = 11;
  t0.struct_.at(0).item = 12;
  t0.enum_.at(0) = json_xdr_unittest::Enum::ONE;
  t0.union_.at(0).set_int32(13);

  XdrWrite(&json, &t0, XdrArrayData);

  json_xdr_unittest::ArrayData t1;
  EXPECT_TRUE(XdrRead(json, &t1, XdrArrayData));

  EXPECT_EQ(t1, t0) << json;

  // Technically not needed because the equality should cover this, but makes it
  // more transparent what's going on.
  EXPECT_EQ("1", t1.string.at(0));
  EXPECT_TRUE(t1.bool_.at(0));
  EXPECT_EQ(2, t1.int8.at(0));
  EXPECT_EQ(3, t1.int16.at(0));
  EXPECT_EQ(4, t1.int32.at(0));
  EXPECT_EQ(5, t1.int64.at(0));
  EXPECT_EQ(6u, t1.uint8.at(0));
  EXPECT_EQ(7u, t1.uint16.at(0));
  EXPECT_EQ(8u, t1.uint32.at(0));
  EXPECT_EQ(9u, t1.uint64.at(0));
  EXPECT_EQ(10.0f, t1.float32.at(0));
  EXPECT_EQ(11.0, t1.float64.at(0));
  EXPECT_EQ(12, t1.struct_.at(0).item);
  EXPECT_EQ(json_xdr_unittest::Enum::ONE, t1.enum_.at(0));
  EXPECT_TRUE(t1.union_.at(0).is_int32());
  EXPECT_EQ(13, t1.union_.at(0).int32());
}

}  // namespace
}  // namespace modular
