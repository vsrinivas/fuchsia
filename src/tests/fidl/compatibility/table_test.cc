// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/cpp/fidl.h>
#include <fidl/test/imported/cpp/fidl.h>

#include <gtest/gtest.h>
#include <src/lib/fxl/test/test_settings.h>
#include <src/tests/fidl/compatibility/helpers.h>
#include <src/tests/fidl/compatibility/hlcpp_client_app.h>

using fidl::test::compatibility::AllTypesTable;
using fidl::test::compatibility::Echo_EchoTableWithError_Result;
using fidl::test::compatibility::RespondWith;
using fidl::test::compatibility::this_is_a_struct;
using fidl::test::compatibility::this_is_a_table;
using fidl::test::compatibility::this_is_a_union;
using fidl::test::compatibility::this_is_a_xunion;

using fidl_test_compatibility_helpers::DataGenerator;
using fidl_test_compatibility_helpers::ExtractShortName;
using fidl_test_compatibility_helpers::ForAllServers;
using fidl_test_compatibility_helpers::ForSomeServers;
using fidl_test_compatibility_helpers::GetServersUnderTest;
using fidl_test_compatibility_helpers::HandlesEq;
using fidl_test_compatibility_helpers::kArbitraryVectorSize;
using fidl_test_compatibility_helpers::PrintSummary;
using fidl_test_compatibility_helpers::Servers;
using fidl_test_compatibility_helpers::Summary;

namespace {

void InitializeAllTypesTable(AllTypesTable* value, DataGenerator& gen) {
  value->set_bool_member(gen.next<bool>());
  value->set_int8_member(gen.next<int8_t>());
  value->set_int16_member(gen.next<int16_t>());
  value->set_int32_member(gen.next<int32_t>());
  value->set_int64_member(gen.next<int64_t>());
  value->set_uint8_member(gen.next<uint8_t>());
  value->set_uint16_member(gen.next<uint16_t>());
  value->set_uint32_member(gen.next<uint32_t>());
  value->set_uint64_member(gen.next<uint64_t>());
  value->set_float32_member(gen.next<float>());
  value->set_float64_member(gen.next<double>());
  value->set_enum_member(gen.choose(fidl::test::compatibility::default_enum::kOne,
                                    fidl::test::compatibility::default_enum::kZero));
  value->set_bits_member(gen.choose(fidl::test::compatibility::default_bits::kOne,
                                    fidl::test::compatibility::default_bits::kTwo));
  value->set_handle_member(gen.next<zx::handle>());
  value->set_string_member(gen.next<std::string>());
  value->set_struct_member(gen.next<this_is_a_struct>());
  value->set_union_member(gen.next<this_is_a_union>());

  std::array<uint32_t, fidl::test::compatibility::arrays_size> array;
  for (size_t i = 0; i < array.size(); i++) {
    array[i] = gen.next<uint32_t>();
  }
  value->set_array_member(array);

  std::vector<uint32_t> vector;
  for (size_t i = 0; i < kArbitraryVectorSize; i++) {
    vector.push_back(gen.next<uint32_t>());
  }
  value->set_vector_member(vector);

  value->set_table_member(gen.next<this_is_a_table>());
  value->set_xunion_member(gen.next<this_is_a_xunion>());
}

void ExpectAllTypesTableEq(const AllTypesTable& a, const AllTypesTable& b) {
  EXPECT_TRUE(fidl::Equals(a.bool_member(), b.bool_member()));
  EXPECT_TRUE(fidl::Equals(a.int8_member(), b.int8_member()));
  EXPECT_TRUE(fidl::Equals(a.int16_member(), b.int16_member()));
  EXPECT_TRUE(fidl::Equals(a.int32_member(), b.int32_member()));
  EXPECT_TRUE(fidl::Equals(a.int64_member(), b.int64_member()));
  EXPECT_TRUE(fidl::Equals(a.uint8_member(), b.uint8_member()));
  EXPECT_TRUE(fidl::Equals(a.uint16_member(), b.uint16_member()));
  EXPECT_TRUE(fidl::Equals(a.uint32_member(), b.uint32_member()));
  EXPECT_TRUE(fidl::Equals(a.uint64_member(), b.uint64_member()));
  EXPECT_TRUE(fidl::Equals(a.float32_member(), b.float32_member()));
  EXPECT_TRUE(fidl::Equals(a.float64_member(), b.float64_member()));
  EXPECT_TRUE(fidl::Equals(a.enum_member(), b.enum_member()));
  EXPECT_TRUE(fidl::Equals(a.bits_member(), b.bits_member()));
  EXPECT_TRUE(HandlesEq(a.handle_member(), b.handle_member()));
  EXPECT_TRUE(fidl::Equals(a.string_member(), b.string_member()));
  EXPECT_TRUE(fidl::Equals(a.struct_member(), b.struct_member()));
  EXPECT_TRUE(fidl::Equals(a.union_member(), b.union_member()));
  EXPECT_TRUE(fidl::Equals(a.array_member(), b.array_member()));
  EXPECT_TRUE(fidl::Equals(a.vector_member(), b.vector_member()));
  EXPECT_TRUE(fidl::Equals(a.table_member(), b.table_member()));
  EXPECT_TRUE(fidl::Equals(a.xunion_member(), b.xunion_member()));
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

Servers servers;
Summary summary;

TEST(Table, EchoTable) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
        false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    AllTypesTable sent;
    InitializeAllTypesTable(&sent, generator);

    AllTypesTable sent_clone;
    sent.Clone(&sent_clone);
    AllTypesTable resp_clone;
    bool called_back = false;
    proxy->EchoTable(std::move(sent), server_url,
                     [&loop, &resp_clone, &called_back](AllTypesTable resp) {
                       ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
                       called_back = true;
                       loop.Quit();
                     });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectAllTypesTableEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
        true;
  });
}

TEST(Table, EchoTableWithErrorSuccessCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (table result success)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    AllTypesTable sent;
    InitializeAllTypesTable(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    AllTypesTable sent_clone;
    sent.Clone(&sent_clone);
    AllTypesTable resp_clone;
    bool called_back = false;
    proxy->EchoTableWithError(
        std::move(sent), err, server_url, RespondWith::SUCCESS,
        [&loop, &resp_clone, &called_back](Echo_EchoTableWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().value.Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectAllTypesTableEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (table result success)"] = true;
  });
}

TEST(Table, EchoTableWithErrorErrorCase) {
  ForAllServers(servers,
                // See: fxbug.dev/7966
                [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
                  summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
                          " (table result error)"] = false;
                  // Using randomness to avoid having to come up with varied values by
                  // hand. Seed deterministically so that this function's outputs are
                  // predictable.
                  DataGenerator generator(0xF1D7);

                  AllTypesTable sent;
                  InitializeAllTypesTable(&sent, generator);
                  auto err = fidl::test::compatibility::default_enum::kOne;

                  bool called_back = false;
                  proxy->EchoTableWithError(
                      std::move(sent), err, server_url, RespondWith::ERR,
                      [&loop, &err, &called_back](Echo_EchoTableWithError_Result resp) {
                        ASSERT_TRUE(resp.is_err());
                        ASSERT_EQ(err, resp.err());
                        called_back = true;
                        loop.Quit();
                      });

                  loop.Run();
                  ASSERT_TRUE(called_back);
                  summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
                          " (table result error)"] = true;
                });
}

TEST(Table, EchoTablePayload) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
        false;
    fidl::test::compatibility::RequestTable sent;
    sent.set_forward_to_server(server_url);
    sent.set_value(42u);

    fidl::test::compatibility::RequestTable sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::compatibility::ResponseTable resp_clone;
    bool called_back = false;

    proxy->EchoTablePayload(std::move(sent), [&loop, &resp_clone, &called_back](
                                                 fidl::test::compatibility::ResponseTable resp) {
      ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
      called_back = true;
      loop.Quit();
    });

    loop.Run();
    ASSERT_TRUE(called_back);
    EXPECT_EQ(sent_clone.has_value(), resp_clone.has_value());
    EXPECT_EQ(sent_clone.value(), resp_clone.value());
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
        true;
  });
}

TEST(Table, EchoTablePayloadWithErrorSuccessCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (table result success)"] = false;
    fidl::test::compatibility::EchoEchoTablePayloadWithErrorRequest sent;
    sent.set_forward_to_server(server_url);
    sent.set_value(42u);
    sent.set_result_variant(fidl::test::compatibility::RespondWith::SUCCESS);

    fidl::test::compatibility::EchoEchoTablePayloadWithErrorRequest sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::compatibility::ResponseTable resp_clone;
    bool called_back = false;

    proxy->EchoTablePayloadWithError(
        std::move(sent),
        [&loop, &resp_clone,
         &called_back](fidl::test::compatibility::Echo_EchoTablePayloadWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    EXPECT_EQ(sent_clone.has_value(), resp_clone.has_value());
    EXPECT_EQ(sent_clone.value(), resp_clone.value());
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (table result success)"] = true;
  });
}

TEST(Table, EchoTablePayloadWithErrorErrorCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (table result success)"] = false;
    fidl::test::compatibility::EchoEchoTablePayloadWithErrorRequest sent;
    auto err = fidl::test::compatibility::default_enum::kOne;
    sent.set_forward_to_server(server_url);
    sent.set_result_err(err);
    sent.set_result_variant(fidl::test::compatibility::RespondWith::ERR);

    fidl::test::compatibility::EchoEchoTablePayloadWithErrorRequest sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::compatibility::ResponseTable resp_clone;
    bool called_back = false;

    proxy->EchoTablePayloadWithError(
        std::move(sent),
        [&loop, &err,
         &called_back](fidl::test::compatibility::Echo_EchoTablePayloadWithError_Result resp) {
          ASSERT_TRUE(resp.is_err());
          ASSERT_EQ(err, resp.err());
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (table result success)"] = true;
  });
}

TEST(Table, EchoTablePayloadNoRetval) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
        false;
    fidl::test::compatibility::RequestTable sent;
    sent.set_forward_to_server(server_url);
    sent.set_value(42u);

    fidl::test::compatibility::RequestTable sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::compatibility::ResponseTable resp_clone;
    bool event_received = false;

    proxy.events().OnEchoTablePayloadEvent =
        [&loop, &resp_clone, &event_received](fidl::test::compatibility::ResponseTable resp) {
          resp.Clone(&resp_clone);
          event_received = true;
          loop.Quit();
        };
    proxy->EchoTablePayloadNoRetVal(std::move(sent));
    loop.Run();
    ASSERT_TRUE(event_received);
    EXPECT_EQ(sent_clone.has_value(), resp_clone.has_value());
    EXPECT_EQ(sent_clone.value(), resp_clone.value());
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
        true;
  });
}

// TODO(fxbug.dev/94910): This is an N+M case, where we only want to test each bindings
// client/server once, rather than in combination with ever other binding. Move this test case to a
// more appropriate file with other such N+M cases, once it exists.
TEST(Table, EchoTableRequestComposed) {
  const auto filter = [](const std::string& proxy_url, const std::string& server_url) -> bool {
    return proxy_url == server_url;
  };

  ForSomeServers(
      servers, filter,
      [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
         const std::string& server_url, const std::string& proxy_url) {
        summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
            false;
        ::fidl::test::imported::ComposedEchoTableRequestComposedRequest sent;
        sent.set_value(42u);
        sent.set_forward_to_server(server_url);

        ::fidl::test::imported::ComposedEchoTableRequestComposedRequest sent_clone;
        sent.Clone(&sent_clone);

        fidl::test::imported::SimpleStruct expected_resp;
        expected_resp.f1 = true;
        expected_resp.f2 = sent.value();

        fidl::test::imported::SimpleStruct resp_clone;
        bool called_back = false;

        proxy->EchoTableRequestComposed(
            std::move(sent),
            [&loop, &resp_clone, &called_back](fidl::test::imported::SimpleStruct resp) {
              ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
              called_back = true;
              loop.Quit();
            });

        loop.Run();
        ASSERT_TRUE(called_back);
        EXPECT_EQ(expected_resp.f1, resp_clone.f1);
        EXPECT_EQ(expected_resp.f2, resp_clone.f2);
        summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
            true;
      });
}

}  // namespace

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  assert(GetServersUnderTest(argc, argv, &servers));

  int r = RUN_ALL_TESTS();
  PrintSummary(summary);
  return r;
}
