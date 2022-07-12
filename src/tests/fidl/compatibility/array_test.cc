// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/cpp/fidl.h>
#include <fidl/test/imported/cpp/fidl.h>

#include <gtest/gtest.h>
#include <src/lib/fxl/test/test_settings.h>
#include <src/tests/fidl/compatibility/helpers.h>
#include <src/tests/fidl/compatibility/hlcpp_client_app.h>

using fidl::test::compatibility::ArraysStruct;
using fidl::test::compatibility::Echo_EchoArraysWithError_Result;
using fidl::test::compatibility::RespondWith;
using fidl::test::compatibility::this_is_a_struct;
using fidl::test::compatibility::this_is_a_table;
using fidl::test::compatibility::this_is_a_union;
using fidl::test::compatibility::this_is_a_xunion;

using fidl_test_compatibility_helpers::DataGenerator;
using fidl_test_compatibility_helpers::ExtractShortName;
using fidl_test_compatibility_helpers::ForAllImpls;
using fidl_test_compatibility_helpers::GetImplsUnderTest;
using fidl_test_compatibility_helpers::HandlesEq;
using fidl_test_compatibility_helpers::Impls;
using fidl_test_compatibility_helpers::PrintSummary;
using fidl_test_compatibility_helpers::Summary;

namespace {

void InitializeArraysStruct(ArraysStruct* value, DataGenerator& gen) {
  for (uint32_t i = 0; i < fidl::test::compatibility::arrays_size; i++) {
    value->bools[i] = gen.next<bool>();
    value->int8s[i] = gen.next<int8_t>();
    value->int16s[i] = gen.next<int16_t>();
    value->int32s[i] = gen.next<int32_t>();
    value->uint8s[i] = gen.next<uint8_t>();
    value->uint16s[i] = gen.next<uint16_t>();
    value->uint32s[i] = gen.next<uint32_t>();
    value->uint64s[i] = gen.next<uint64_t>();
    value->float32s[i] = gen.next<float>();
    value->float64s[i] = gen.next<double>();

    value->enums[i] = gen.choose(fidl::test::compatibility::default_enum::kOne,
                                 fidl::test::compatibility::default_enum::kZero);
    value->bits[i] = gen.choose(fidl::test::compatibility::default_bits::kOne,
                                fidl::test::compatibility::default_bits::kTwo);

    value->handles[i] = gen.next<zx::handle>();
    value->nullable_handles[i] = gen.next<zx::handle>(true);

    value->strings[i] = gen.next<std::string>();
    value->nullable_strings[i] = gen.next<fidl::StringPtr>();

    value->structs[i] = gen.next<this_is_a_struct>();
    value->nullable_structs[i] = gen.next<std::unique_ptr<this_is_a_struct>>();
    if (gen.next<bool>()) {
      value->nullable_structs[i] = std::make_unique<this_is_a_struct>();
      value->nullable_structs[i]->s = gen.next<std::string>();
    }

    value->unions[i] = gen.next<this_is_a_union>();
    value->nullable_unions[i] = gen.next<std::unique_ptr<this_is_a_union>>();

    for (size_t j = 0; j < fidl::test::compatibility::arrays_size; j++) {
      value->arrays[i][j] = gen.next<uint32_t>();
      value->vectors[i].push_back(gen.next<uint32_t>());
    }

    if (gen.next<bool>()) {
      value->nullable_vectors[i].emplace();
      for (size_t j = 0; j < fidl::test::compatibility::arrays_size; j++) {
        value->nullable_vectors[i]->push_back(gen.next<uint32_t>());
      }
    }

    value->tables[i] = gen.next<this_is_a_table>();
    value->xunions[i] = gen.next<this_is_a_xunion>();
  }
}

void ExpectArraysStructEq(const ArraysStruct& a, const ArraysStruct& b) {
  EXPECT_TRUE(fidl::Equals(a.bools, b.bools));
  EXPECT_TRUE(fidl::Equals(a.int8s, b.int8s));
  EXPECT_TRUE(fidl::Equals(a.int16s, b.int16s));
  EXPECT_TRUE(fidl::Equals(a.int32s, b.int32s));
  EXPECT_TRUE(fidl::Equals(a.int64s, b.int64s));
  EXPECT_TRUE(fidl::Equals(a.uint8s, b.uint8s));
  EXPECT_TRUE(fidl::Equals(a.uint16s, b.uint16s));
  EXPECT_TRUE(fidl::Equals(a.uint32s, b.uint32s));
  EXPECT_TRUE(fidl::Equals(a.uint64s, b.uint64s));
  EXPECT_TRUE(fidl::Equals(a.float32s, b.float32s));
  EXPECT_TRUE(fidl::Equals(a.float64s, b.float64s));
  EXPECT_TRUE(fidl::Equals(a.enums, b.enums));
  EXPECT_TRUE(fidl::Equals(a.bits, b.bits));
  EXPECT_EQ(a.handles.size(), b.handles.size());
  EXPECT_EQ(a.nullable_handles.size(), b.nullable_handles.size());
  EXPECT_EQ(a.handles.size(), a.nullable_handles.size());
  for (size_t i = 0; i < a.handles.size(); i++) {
    EXPECT_TRUE(HandlesEq(a.handles[i], b.handles[i]));
    EXPECT_TRUE(HandlesEq(a.nullable_handles[i], b.nullable_handles[i]));
  }
  EXPECT_TRUE(fidl::Equals(a.strings, b.strings));
  EXPECT_TRUE(fidl::Equals(a.nullable_strings, b.nullable_strings));
  EXPECT_TRUE(fidl::Equals(a.structs, b.structs));
  EXPECT_TRUE(fidl::Equals(a.nullable_structs, b.nullable_structs));
  EXPECT_TRUE(fidl::Equals(a.unions, b.unions));
  EXPECT_TRUE(fidl::Equals(a.nullable_unions, b.nullable_unions));
  EXPECT_TRUE(fidl::Equals(a.arrays, b.arrays));
  EXPECT_TRUE(fidl::Equals(a.vectors, b.vectors));
  EXPECT_TRUE(fidl::Equals(a.nullable_vectors, b.nullable_vectors));
  EXPECT_TRUE(fidl::Equals(a.tables, b.tables));
  EXPECT_TRUE(fidl::Equals(a.xunions, b.xunions));
}

class CompatibilityTest : public ::testing::TestWithParam<std::tuple<std::string, std::string>> {
 protected:
  void SetUp() override {
    proxy_url_ = ::testing::get<0>(GetParam());
    server_url_ = ::testing::get<1>(GetParam());
    // The FIDL support lib requires async_get_default_dispatcher() to return
    // non-null.
    loop_.reset(new async::Loop(&kAsyncLoopConfigAttachToCurrentThread));
  }
  std::string proxy_url_;
  std::string server_url_;
  std::unique_ptr<async::Loop> loop_;
};

Impls impls;
Summary summary;

TEST(Array, EchoArrays) {
  ForAllImpls(impls, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                        const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (array)"] =
        false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    ArraysStruct sent;
    InitializeArraysStruct(&sent, generator);

    ArraysStruct sent_clone;
    sent.Clone(&sent_clone);
    ArraysStruct resp_clone;
    bool called_back = false;
    proxy->EchoArrays(std::move(sent), server_url,
                      [&loop, &resp_clone, &called_back](ArraysStruct resp) {
                        ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
                        called_back = true;
                        loop.Quit();
                      });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectArraysStructEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (array)"] =
        true;
  });
}

TEST(Array, EchoArraysWithErrorSuccessCase) {
  ForAllImpls(impls, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                        const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (array result success)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    ArraysStruct sent;
    InitializeArraysStruct(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    ArraysStruct sent_clone;
    sent.Clone(&sent_clone);
    ArraysStruct resp_clone;
    bool called_back = false;
    proxy->EchoArraysWithError(
        std::move(sent), err, server_url, RespondWith::SUCCESS,
        [&loop, &resp_clone, &called_back](Echo_EchoArraysWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().value.Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectArraysStructEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (array result success)"] = true;
  });
}

TEST(Array, EchoArraysWithErrorErrorCase) {
  ForAllImpls(impls, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                        const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (array result error)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    ArraysStruct sent;
    InitializeArraysStruct(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    bool called_back = false;
    proxy->EchoArraysWithError(std::move(sent), err, server_url, RespondWith::ERR,
                               [&loop, &err, &called_back](Echo_EchoArraysWithError_Result resp) {
                                 ASSERT_TRUE(resp.is_err());
                                 ASSERT_EQ(err, resp.err());
                                 called_back = true;
                                 loop.Quit();
                               });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (array result error)"] = true;
  });
}

}  // namespace

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  assert(GetImplsUnderTest(argc, argv, &impls));

  int r = RUN_ALL_TESTS();
  PrintSummary(summary);
  return r;
}
