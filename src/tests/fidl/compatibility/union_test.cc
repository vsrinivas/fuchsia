// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/cpp/fidl.h>
#include <fidl/test/imported/cpp/fidl.h>

#include <gtest/gtest.h>
#include <src/lib/fxl/test/test_settings.h>
#include <src/tests/fidl/compatibility/helpers.h>
#include <src/tests/fidl/compatibility/hlcpp_client_app.h>

using fidl::test::compatibility::AllTypesXunion;
using fidl::test::compatibility::Echo_EchoXunionsWithError_Result;
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

void InitializeAllTypesXunions(std::vector<AllTypesXunion>* value, DataGenerator& gen) {
  for (size_t i = 1; true; i++) {
    AllTypesXunion xu{};
    switch (i) {
      case 1:
        xu.set_bool_member(gen.next<bool>());
        break;
      case 2:
        xu.set_int8_member(gen.next<int8_t>());
        break;
      case 3:
        xu.set_int16_member(gen.next<int16_t>());
        break;
      case 4:
        xu.set_int32_member(gen.next<int32_t>());
        break;
      case 5:
        xu.set_int64_member(gen.next<int64_t>());
        break;
      case 6:
        xu.set_uint8_member(gen.next<uint8_t>());
        break;
      case 7:
        xu.set_uint16_member(gen.next<uint16_t>());
        break;
      case 8:
        xu.set_uint32_member(gen.next<uint32_t>());
        break;
      case 9:
        xu.set_uint64_member(gen.next<uint64_t>());
        break;
      case 10:
        xu.set_float32_member(gen.next<float>());
        break;
      case 11:
        xu.set_float64_member(gen.next<double>());
        break;
      case 12:
        xu.set_enum_member(gen.choose(fidl::test::compatibility::default_enum::kOne,
                                      fidl::test::compatibility::default_enum::kZero));
        break;
      case 13:
        xu.set_bits_member(gen.choose(fidl::test::compatibility::default_bits::kOne,
                                      fidl::test::compatibility::default_bits::kTwo));
        break;
      case 14:
        xu.set_handle_member(gen.next<zx::handle>());
        break;
      case 15:
        xu.set_string_member(gen.next<std::string>());
        break;
      case 16:
        xu.set_struct_member(gen.next<this_is_a_struct>());
        break;
      case 17:
        xu.set_union_member(gen.next<this_is_a_union>());
        break;
      default:
        EXPECT_EQ(i, 18UL);
        return;
    }
    value->push_back(std::move(xu));
  }
}

void ExpectAllTypesXunionsEq(const std::vector<AllTypesXunion>& a,
                             const std::vector<AllTypesXunion>& b) {
  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].is_handle_member()) {
      EXPECT_TRUE(b[i].is_handle_member());
      EXPECT_TRUE(HandlesEq(a[i].handle_member(), b[i].handle_member()));
    } else {
      EXPECT_TRUE(fidl::Equals(a[i], b[i]));
    }
  }
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

TEST(Union, EchoUnions) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (xunion)"] =
        false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    std::vector<AllTypesXunion> sent;
    InitializeAllTypesXunions(&sent, generator);

    std::vector<AllTypesXunion> sent_clone;
    fidl::Clone(sent, &sent_clone);
    std::vector<AllTypesXunion> resp_clone;
    bool called_back = false;
    proxy->EchoXunions(std::move(sent), server_url,
                       [&loop, &resp_clone, &called_back](std::vector<AllTypesXunion> resp) {
                         ASSERT_EQ(ZX_OK, fidl::Clone(resp, &resp_clone));
                         called_back = true;
                         loop.Quit();
                       });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectAllTypesXunionsEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (xunion)"] =
        true;
  });
}

TEST(Union, EchoUnionsWithErrorSuccessCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (xunion result success)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    std::vector<AllTypesXunion> sent;
    InitializeAllTypesXunions(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    std::vector<AllTypesXunion> sent_clone;
    fidl::Clone(sent, &sent_clone);
    std::vector<AllTypesXunion> resp_clone;
    bool called_back = false;
    proxy->EchoXunionsWithError(
        std::move(sent), err, server_url, RespondWith::SUCCESS,
        [&loop, &resp_clone, &called_back](Echo_EchoXunionsWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, fidl::Clone(resp.response().value, &resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectAllTypesXunionsEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (xunion result success)"] = true;
  });
}

TEST(Union, EchoUnionsWithErrorErrorCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (xunion result error)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    std::vector<AllTypesXunion> sent;
    InitializeAllTypesXunions(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    bool called_back = false;
    proxy->EchoXunionsWithError(std::move(sent), err, server_url, RespondWith::ERR,
                                [&loop, &err, &called_back](Echo_EchoXunionsWithError_Result resp) {
                                  ASSERT_TRUE(resp.is_err());
                                  ASSERT_EQ(err, resp.err());
                                  called_back = true;
                                  loop.Quit();
                                });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (xunion result error)"] = true;
  });
}

TEST(Union, EchoUnionPayload) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (union)"] =
        false;
    fidl::test::compatibility::RequestUnion sent;
    sent.set_signed_({.value = -123, .forward_to_server = server_url});

    fidl::test::compatibility::RequestUnion sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::compatibility::ResponseUnion resp_clone;
    bool called_back = false;

    proxy->EchoUnionPayload(std::move(sent), [&loop, &resp_clone, &called_back](
                                                 fidl::test::compatibility::ResponseUnion resp) {
      ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
      called_back = true;
      loop.Quit();
    });

    loop.Run();
    ASSERT_TRUE(called_back);
    EXPECT_EQ(sent_clone.is_signed_(), resp_clone.is_signed_());
    EXPECT_EQ(sent_clone.signed_().value, resp_clone.signed_());
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (union)"] =
        true;
  });
}

TEST(Union, EchoUnionPayloadWithErrorSuccessCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (union result success)"] = false;
    fidl::test::compatibility::UnsignedErrorable unsigned_errorable;
    unsigned_errorable.forward_to_server = server_url;
    unsigned_errorable.value = 42u;
    unsigned_errorable.result_variant = fidl::test::compatibility::RespondWith::SUCCESS;

    auto sent = fidl::test::compatibility::EchoEchoUnionPayloadWithErrorRequest::WithUnsigned_(
        std::move(unsigned_errorable));

    fidl::test::compatibility::EchoEchoUnionPayloadWithErrorRequest sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::compatibility::ResponseUnion resp_clone;
    bool called_back = false;

    proxy->EchoUnionPayloadWithError(
        std::move(sent),
        [&loop, &resp_clone,
         &called_back](fidl::test::compatibility::Echo_EchoUnionPayloadWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    EXPECT_EQ(sent_clone.is_unsigned_(), resp_clone.is_unsigned_());
    EXPECT_EQ(sent_clone.unsigned_().value, resp_clone.unsigned_());
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (union result success)"] = true;
  });
}

TEST(Union, EchoUnionPayloadWithErrorErrorCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (union result success)"] = false;
    fidl::test::compatibility::SignedErrorable signed_errorable;
    auto err = fidl::test::compatibility::default_enum::kOne;
    signed_errorable.forward_to_server = server_url;
    signed_errorable.result_err = err;
    signed_errorable.result_variant = fidl::test::compatibility::RespondWith::ERR;

    auto sent = fidl::test::compatibility::EchoEchoUnionPayloadWithErrorRequest::WithSigned_(
        std::move(signed_errorable));

    fidl::test::compatibility::EchoEchoUnionPayloadWithErrorRequest sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::compatibility::ResponseUnion resp_clone;
    bool called_back = false;

    proxy->EchoUnionPayloadWithError(
        std::move(sent),
        [&loop, &err,
         &called_back](fidl::test::compatibility::Echo_EchoUnionPayloadWithError_Result resp) {
          ASSERT_TRUE(resp.is_err());
          ASSERT_EQ(err, resp.err());
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (union result success)"] = true;
  });
}

TEST(Union, EchoUnionPayloadNoRetval) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (union)"] =
        false;
    fidl::test::compatibility::RequestUnion sent;
    sent.set_unsigned_({.value = 42u, .forward_to_server = server_url});

    fidl::test::compatibility::RequestUnion sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::compatibility::ResponseUnion resp_clone;
    bool event_received = false;

    proxy.events().OnEchoUnionPayloadEvent =
        [&loop, &resp_clone, &event_received](fidl::test::compatibility::ResponseUnion resp) {
          resp.Clone(&resp_clone);
          event_received = true;
          loop.Quit();
        };
    proxy->EchoUnionPayloadNoRetVal(std::move(sent));
    loop.Run();
    ASSERT_TRUE(event_received);
    EXPECT_EQ(sent_clone.is_unsigned_(), resp_clone.is_unsigned_());
    EXPECT_EQ(sent_clone.unsigned_().value, resp_clone.unsigned_());
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (union)"] =
        true;
  });
}

// TODO(fxbug.dev/94910): This is an N+M case, where we only want to test each bindings
// client/server once, rather than in combination with ever other binding. Move this test case to a
// more appropriate file with other such N+M cases, once it exists.
TEST(Union, EchoUnionResponseWithErrorComposedSuccessCase) {
  const auto filter = [](const std::string& proxy_url, const std::string& server_url) -> bool {
    return proxy_url == server_url;
  };

  ForSomeServers(
      servers, filter,
      [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
         const std::string& server_url, const std::string& proxy_url) {
        summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (union)"] =
            false;
        int64_t value = -42;
        bool want_absolute_value = true;
        const uint32_t err = 13;

        ::fidl::test::imported::Composed_EchoUnionResponseWithErrorComposed_Response resp_clone;
        bool called_back = false;

        proxy->EchoUnionResponseWithErrorComposed(
            value, want_absolute_value, server_url, err,
            fidl::test::imported::WantResponse::SUCCESS,
            [&loop, &resp_clone, &called_back](
                fidl::test::imported::Composed_EchoUnionResponseWithErrorComposed_Result resp) {
              ASSERT_TRUE(resp.is_response());
              ASSERT_EQ(ZX_OK, resp.response().Clone(&resp_clone));
              called_back = true;
              loop.Quit();
            });

        loop.Run();
        ASSERT_TRUE(called_back);
        ASSERT_EQ(want_absolute_value, resp_clone.is_unsigned_());
        EXPECT_EQ((uint64_t)std::abs(value), resp_clone.unsigned_());
        summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (union)"] =
            true;
      });
}

// TODO(fxbug.dev/94910): This is an N+M case, where we only want to test each bindings
// client/server once, rather than in combination with ever other binding. Move this test case to a
// more appropriate file with other such N+M cases, once it exists.
TEST(Union, EchoUnionResponseWithErrorComposedErrorCase) {
  const auto filter = [](const std::string& proxy_url, const std::string& server_url) -> bool {
    return proxy_url == server_url;
  };

  ForSomeServers(
      servers, filter,
      [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
         const std::string& server_url, const std::string& proxy_url) {
        summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (union)"] =
            false;
        int64_t value = -42;
        bool want_absolute_value = true;
        const uint32_t err = 13;
        bool called_back = false;

        proxy->EchoUnionResponseWithErrorComposed(
            value, want_absolute_value, server_url, err, fidl::test::imported::WantResponse::ERR,
            [&loop, &err, &called_back](
                fidl::test::imported::Composed_EchoUnionResponseWithErrorComposed_Result resp) {
              ASSERT_TRUE(resp.is_err());
              ASSERT_EQ(err, resp.err());
              called_back = true;
              loop.Quit();
            });

        loop.Run();
        ASSERT_TRUE(called_back);
        summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (union)"] =
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
