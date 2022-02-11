// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"
#include "src/lib/fxl/test/test_settings.h"

#define DISABLED_FOR(b)        \
  if (target_binding == (b)) { \
    return;                    \
  }

namespace {

// To find all ordinals:
//
//     cat
//     out/default/fidling/gen/src/tests/fidl/dyn_suite/fidl.dynsuite/fidl.dynsuite/llcpp/fidl/fidl.dynsuite/cpp/wire_messaging.cc
//     | grep -e 'constexpr.*kBase.*Ordinal' -A 1
//
// While using `jq` would be much nicer, large numbers are mishandled and the
// displayed ordinal ends up being incorrect.
static const uint64_t kOrdinalOneWayInteractionNoPayload = 6896935086133512518lu;

static const std::string kGo = "go";
static const std::string kHlcpp = "hlcpp";

std::string target_binding = {};

}  // namespace

TEST_F(ServerTest, Bad_ClientClosingChannelCausesUnbind) {
  when([&]() {
    zx_handle_close(client_end);
  }).wait_for([&](auto observations) {
      return observations.has(Observation::Kind::kOnComplete);
    }).then_observe([&](auto observations) {
    // We are not opinionated about what has been observed, just that the last
    // two observations must be unbinding and completion.
    EXPECT_TRUE(2u <= observations.size());
    auto last = observations.size() - 1;
    EXPECT_EQ(Observation::Kind::kOnUnbind, observations[last - 1].kind());
    EXPECT_EQ(Observation::Kind::kOnComplete, observations[last].kind());
  });
}

TEST_F(ServerTest, Bad_WrongOrdinalCausesUnbind) {
  when([&]() {
    fidl_message_header_t hdr;
    fidl_init_txn_header(&hdr, 0, /* some wrong ordinal */ 8888888lu);
    zx_channel_write(client_end, 0, &hdr, sizeof(fidl_message_header_t), nullptr, 0);
  }).wait_for([&](auto observations) {
      return observations.has(Observation::Kind::kOnComplete);
    }).then_observe([&](auto observations) {
    // Some bindings observe an error, which will precede unbinding.
    EXPECT_TRUE(2u <= observations.size());
    auto last = observations.size() - 1;
    EXPECT_EQ(Observation::Kind::kOnUnbind, observations[last - 1].kind());
    EXPECT_EQ(Observation::Kind::kOnComplete, observations[last].kind());
  });

  zx_handle_close(client_end);
}

TEST_F(ServerTest, Good_OneWayInteraction) {
  // TODO(fxbug.dev/92603): Should work on HLCPP.
  DISABLED_FOR(kHlcpp);

  when([&]() {
    fidl_message_header_t hdr;
    fidl_init_txn_header(&hdr, 0, kOrdinalOneWayInteractionNoPayload);
    zx_channel_write(client_end, 0, &hdr, sizeof(fidl_message_header_t), nullptr, 0);
  }).wait_for([&](auto observations) {
      return 2 <= observations.size();
    }).then_observe([&](auto observations) {
    EXPECT_EQ(2u, observations.size());
    EXPECT_EQ(Observation::Kind::kOnMethodInvocation, observations[0].kind());
    EXPECT_EQ(Observation::Kind::kOnMethodInvocation, observations[1].kind());
  });

  zx_handle_close(client_end);
}

TEST_F(ServerTest, Bad_OneWayInteractionWithTxIdNotZero) {
  // TODO(fxbug.dev/92604): Should work on Go.
  DISABLED_FOR(kGo);

  when([&]() {
    fidl_message_header_t hdr;
    fidl_init_txn_header(&hdr, 56 /* txid not 0 */, kOrdinalOneWayInteractionNoPayload);
    zx_channel_write(client_end, 0, &hdr, sizeof(fidl_message_header_t), nullptr, 0);
  }).wait_for([&](auto observations) {
      return 2 <= observations.size();
    }).then_observe([&](auto observations) {
    // Some bindings observe an error, which will precede unbinding.
    EXPECT_TRUE(2u <= observations.size());
    auto last = observations.size() - 1;
    EXPECT_EQ(Observation::Kind::kOnUnbind, observations[last - 1].kind());
    EXPECT_EQ(Observation::Kind::kOnComplete, observations[last].kind());
  });

  zx_handle_close(client_end);
}

TEST_F(ClientTest, Good_ServerClosesChannel) {
  // TODO(fxbug.dev/92604): Should work on Go.
  DISABLED_FOR(kGo);

  when([&]() {
    zx_handle_close(server_end);
  }).wait_for([&](auto observations) {
      return 2 <= observations.size();
    }).then_observe([&](auto observations) {
    // Some bindings observe an error, which will precede unbinding.
    EXPECT_TRUE(2u <= observations.size());
    auto last = observations.size() - 1;
    EXPECT_EQ(Observation::Kind::kOnUnbind, observations[last - 1].kind());
    EXPECT_EQ(Observation::Kind::kOnComplete, observations[last].kind());
  });
}

namespace {
static const std::string gtest_enumeration_flag = "--gtest_list_tests";
static const std::string target_flag = "--target";
}  // namespace

int main(int argc, char** argv) {
  // When running this binary in enumeration mode (or discovery mode), the
  // custom arguments specified in the CML are not provided. It is therefore
  // important to identify that are running in this mode to skip any validation
  // we do on arguments.
  for (int i = 1; i < argc; i++) {
    if (argv[i] == gtest_enumeration_flag) {
      goto run_all_tests;
    }
  }

  for (int i = 1; i < argc; i++) {
    if (auto arg = std::string(argv[i]); arg.rfind(target_flag, 0) == 0) {
      target_binding = arg.substr(target_flag.size() + 1);
    }
  }
  if (target_binding != kGo && target_binding != kHlcpp) {
    return EXIT_FAILURE;
  }

run_all_tests:
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
