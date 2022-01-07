// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/diagnostics/inspect/codelab/cpp/testing/integration_test.h"

#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <memory>

namespace codelab::testing {

namespace {
constexpr char fizzbuzz_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_integration_tests#meta/"
    "fizzbuzz.cm";
constexpr char reverser_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_integration_tests#meta/"
    "part_5.cm";
}  // namespace

fuchsia::examples::inspect::ReverserPtr IntegrationTest::ConnectToReverser(TestOptions options) {
  auto realm_builder = sys::testing::experimental::RealmBuilder::Create();
  realm_builder.AddChild("reverser", reverser_url);

  if (options.include_fizzbuzz) {
    realm_builder.AddChild("fizzbuzz", fizzbuzz_url);
    realm_builder
        .AddRoute(sys::testing::Route{
            .capabilities = {sys::testing::Protocol{"fuchsia.examples.inspect.FizzBuzz"}},
            .source = sys::testing::ChildRef{"fizzbuzz"},
            .targets = {sys::testing::ChildRef{"reverser"}}})
        .AddRoute(
            sys::testing::Route{.capabilities = {sys::testing::Protocol{"fuchsia.logger.LogSink"}},
                                .source = sys::testing::ParentRef(),
                                .targets = {sys::testing::ChildRef{"fizzbuzz"}}});
  }

  realm_builder
      .AddRoute(sys::testing::Route{
          .capabilities = {sys::testing::Protocol{"fuchsia.examples.inspect.Reverser"}},
          .source = sys::testing::ChildRef{"reverser"},
          .targets = {sys::testing::ParentRef()}})
      .AddRoute(
          sys::testing::Route{.capabilities = {sys::testing::Protocol{"fuchsia.logger.LogSink"}},
                              .source = sys::testing::ParentRef(),
                              .targets = {sys::testing::ChildRef{"reverser"}}});
  realm_ = std::make_unique<sys::testing::experimental::RealmRoot>(realm_builder.Build());
  fuchsia::examples::inspect::ReverserPtr proxy;
  realm_->Connect(proxy.NewRequest());
  return proxy;
}

std::string IntegrationTest::ReverserMonikerForSelectors() const {
  auto root_name = realm_->GetChildName();
  return "realm_builder\\:" + root_name + "/reverser";
}

}  // namespace codelab::testing
