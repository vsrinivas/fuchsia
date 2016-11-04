// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gtest/gtest.h"
#include "lib/fidl/compiler/interfaces/tests/sample_interfaces.fidl.h"

using sample::SampleInterface;
using sample::SampleInterface_SampleMethod1_Params;
using sample::SampleInterface_SampleMethod1_ResponseParams;

namespace fidl {
namespace test {
namespace {

// Test the validity of the generated ordinals for interface methods.
TEST(SampleInterfaceOrdinals, CheckGeneratedOrdinals) {
  EXPECT_EQ(0U, static_cast<uint32_t>(
                    SampleInterface::MessageOrdinals::SampleMethod0));
  EXPECT_EQ(1U, static_cast<uint32_t>(
                    SampleInterface::MessageOrdinals::SampleMethod1));
  EXPECT_EQ(2U, static_cast<uint32_t>(
                    SampleInterface::MessageOrdinals::SampleMethod2));
}

// Test that a request params struct is generated for interface methods, and
// test that it is serializable.
TEST(SampleInterfaceGeneratedStructs, RequestStruct) {
  SampleInterface_SampleMethod1_Params params;
  params.in1 = 123;
  params.in2.reset();

  size_t size = params.GetSerializedSize();
  EXPECT_EQ(8U            // Struct header
                + (4U     // |in1| field
                   + 8U)  // |in2| offset to string (which is null)
                + 4U,     // padding to make the struct 8-byte aligned
            size);

  std::unique_ptr<char[]> bytes(new char[size]);
  EXPECT_TRUE(params.Serialize(bytes.get(), size));

  auto* params_data = reinterpret_cast<
      sample::internal::SampleInterface_SampleMethod1_Params_Data*>(
      bytes.get());
  EXPECT_EQ(123, params_data->in1);
  EXPECT_EQ(0UL, params_data->in2.offset);
}

// Test that a response params struct is generated for interface methods, and
// test that it is serializable.
TEST(SampleInterfaceGeneratedStructs, ResponseStruct) {
  SampleInterface_SampleMethod1_ResponseParams params;
  params.out1.reset();
  params.out2 = sample::Enum::VALUE;

  size_t size = params.GetSerializedSize();
  EXPECT_EQ(8U            // Struct header
                + (8U     // |out1| offset to string (which is null)
                   + 4U)  // |out2| enum
                + 4U,     // padding to make the struct 8-byte aligned
            size);

  std::unique_ptr<char[]> bytes(new char[size]);
  EXPECT_TRUE(params.Serialize(bytes.get(), size));

  auto* params_data = reinterpret_cast<
      sample::internal::SampleInterface_SampleMethod1_ResponseParams_Data*>(
      bytes.get());
  EXPECT_EQ(0UL, params_data->out1.offset);
  EXPECT_EQ(sample::Enum::VALUE, static_cast<sample::Enum>(params_data->out2));
}

}  // namespace
}  // namespace test
}  // namespace fidl
