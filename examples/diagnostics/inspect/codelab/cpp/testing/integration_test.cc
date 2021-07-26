// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/diagnostics/inspect/codelab/cpp/testing/integration_test.h"

#include <lib/sys/cpp/testing/realm_builder.h>

namespace codelab::testing {

namespace {
constexpr char fizzbuzz_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_integration_tests#meta/"
    "fizzbuzz.cm";
constexpr char reverser_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_integration_tests#meta/"
    "part_5.cm";
};  // namespace

void FizzBuzzMock::Start(std::unique_ptr<sys::testing::MockHandles> mock_handles) {
  mock_handles_ = std::move(mock_handles);
  ASSERT_EQ(mock_handles_->outgoing()->AddPublicService<fuchsia::examples::inspect::FizzBuzz>(
                [](auto request) {
                  // We drop the request intentionally for testing purposes.
                }),
            ZX_OK);
}

fuchsia::examples::inspect::ReverserPtr IntegrationTest::ConnectToReverser(TestOptions options) {
  auto context = sys::ComponentContext::Create();
  auto realm_builder = sys::testing::Realm::Builder::New(context.get());
  if (options.include_fizzbuzz) {
    realm_builder.AddComponent(
        sys::testing::Moniker{"fizzbuzz"},
        sys::testing::Component{.source = sys::testing::ComponentUrl{fizzbuzz_url}});
  } else {
    fizzbuzz_mock_ = std::make_unique<FizzBuzzMock>();
    realm_builder.AddComponent(
        sys::testing::Moniker{"fizzbuzz"},
        sys::testing::Component{.source = sys::testing::Mock{fizzbuzz_mock_.get()},
                                .eager = false});
  }
  realm_builder
      .AddComponent(sys::testing::Moniker{"reverser"},
                    sys::testing::Component{.source = sys::testing::ComponentUrl{reverser_url}})
      .AddRoute(sys::testing::CapabilityRoute{
          .capability = sys::testing::Protocol{"fuchsia.examples.inspect.Reverser"},
          .source = sys::testing::Moniker{"reverser"},
          .targets = {sys::testing::AboveRoot()}})
      .AddRoute(sys::testing::CapabilityRoute{
          .capability = sys::testing::Protocol{"fuchsia.examples.inspect.FizzBuzz"},
          .source = sys::testing::Moniker{"fizzbuzz"},
          .targets = {sys::testing::Moniker{"reverser"}}})
      .AddRoute(sys::testing::CapabilityRoute{
          .capability = sys::testing::Protocol{"fuchsia.logger.LogSink"},
          .source = sys::testing::AboveRoot(),
          .targets = {sys::testing::Moniker{"fizzbuzz"}, sys::testing::Moniker{"reverser"}}});
  realm_.emplace(realm_builder.Build());
  fuchsia::examples::inspect::ReverserPtr proxy;
  realm_->Connect(proxy.NewRequest());
  return proxy;
}

std::string IntegrationTest::ReverserMonikerForSelectors() const {
  auto root_name = realm_->GetChildName();
  return "fuchsia_component_test_collection\\:" + root_name + "/reverser";
}

};  // namespace codelab::testing
