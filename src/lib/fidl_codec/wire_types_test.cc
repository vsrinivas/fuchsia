// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/wire_types.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

#include "src/lib/fidl_codec/fidl_codec_test.h"
#include "src/lib/fidl_codec/library_loader.h"

namespace fidl_codec {

class TypeTest : public ::testing::Test {
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

 private:
  LibraryLoader* loader_ = nullptr;
  Library* library_ = nullptr;
};

TEST_F(TypeTest, CppName) {
  EXPECT_EQ(BoolType().CppName(), "bool");
  EXPECT_EQ(Int8Type().CppName(), "int8_t");
  EXPECT_EQ(Int16Type().CppName(), "int16_t");
  EXPECT_EQ(Int32Type().CppName(), "int32_t");
  EXPECT_EQ(Int64Type().CppName(), "int64_t");
  EXPECT_EQ(Uint8Type().CppName(), "uint8_t");
  EXPECT_EQ(Uint16Type().CppName(), "uint16_t");
  EXPECT_EQ(Uint32Type().CppName(), "uint32_t");
  EXPECT_EQ(Uint64Type().CppName(), "uint64_t");
  EXPECT_EQ(StringType().CppName(), "std::string");
  EXPECT_EQ(Float32Type().CppName(), "float");
  EXPECT_EQ(Float64Type().CppName(), "double");
  EXPECT_EQ(ArrayType(std::make_unique<BoolType>(), 42).CppName(), "std::array<bool, 42>");
  EXPECT_EQ(VectorType(std::make_unique<BoolType>()).CppName(), "std::vector<bool>");
  EXPECT_EQ(HandleType().CppName(), "zx::handle");
  EXPECT_EQ(StructType(Struct("foo.bar/Baz"), false).CppName(), "foo::bar::Baz");

  std::unique_ptr<Type> table_type =
      library()->TypeFromIdentifier(false, "test.fidlcodec.examples/ValueTable");
  EXPECT_EQ(table_type.get()->CppName(), "test::fidlcodec::examples::ValueTable");

  std::unique_ptr<Type> enum_type =
      library()->TypeFromIdentifier(false, "test.fidlcodec.examples/DefaultEnum");
  EXPECT_EQ(enum_type.get()->CppName(), "test::fidlcodec::examples::DefaultEnum");

  std::unique_ptr<Type> bits_type =
      library()->TypeFromIdentifier(false, "test.fidlcodec.examples/DefaultBits");
  EXPECT_EQ(bits_type.get()->CppName(), "test::fidlcodec::examples::DefaultBits");
}

}  // namespace fidl_codec
