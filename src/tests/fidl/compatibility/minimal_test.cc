// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/cpp/fidl.h>
#include <fidl/test/imported/cpp/fidl.h>

#include <gtest/gtest.h>
#include <src/lib/fxl/test/test_settings.h>
#include <src/tests/fidl/compatibility/helpers.h>
#include <src/tests/fidl/compatibility/hlcpp_client_app.h>

using fidl::test::compatibility::Echo_EchoMinimalWithError_Result;
using fidl::test::compatibility::RespondWith;

using fidl_test_compatibility_helpers::ExtractShortName;
using fidl_test_compatibility_helpers::ForAllImpls;
using fidl_test_compatibility_helpers::GetImplsUnderTest;
using fidl_test_compatibility_helpers::Impls;
using fidl_test_compatibility_helpers::PrintSummary;
using fidl_test_compatibility_helpers::Summary;

namespace {

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

TEST(Minimal, EchoMinimal) {
  ForAllImpls(impls, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                        const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (minimal)"] =
        false;

    bool called_back = false;
    proxy->EchoMinimal(server_url, [&loop, &called_back]() {
      called_back = true;
      loop.Quit();
    });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (minimal)"] =
        true;
  });
}

TEST(Minimal, EchoMinimalWithErrorSuccessCase) {
  ForAllImpls(impls, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                        const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (minimal result success)"] = false;

    bool called_back = false;
    proxy->EchoMinimalWithError(server_url, RespondWith::SUCCESS,
                                [&loop, &called_back](Echo_EchoMinimalWithError_Result resp) {
                                  ASSERT_TRUE(resp.is_response());
                                  called_back = true;
                                  loop.Quit();
                                });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (minimal result success)"] = true;
  });
}

TEST(Minimal, EchoMinimalWithErrorErrorCase) {
  ForAllImpls(impls, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                        const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (minimal result error)"] = false;

    bool called_back = false;
    proxy->EchoMinimalWithError(server_url, RespondWith::ERR,
                                [&loop, &called_back](Echo_EchoMinimalWithError_Result resp) {
                                  ASSERT_TRUE(resp.is_err());
                                  ASSERT_EQ(0u, resp.err());
                                  called_back = true;
                                  loop.Quit();
                                });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (minimal result error)"] = true;
  });
}

TEST(Minimal, EchoMinimalNoRetval) {
  ForAllImpls(impls, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                        const std::string& server_url, const std::string proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (minimal_no_ret)"] = false;

    bool event_received = false;
    proxy.events().EchoMinimalEvent = [&loop, &event_received]() {
      event_received = true;
      loop.Quit();
    };
    proxy->EchoMinimalNoRetVal(server_url);
    loop.Run();
    ASSERT_TRUE(event_received);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (minimal_no_ret)"] = true;
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
