// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/argument_value.h"

#include <string>
#include <vector>

#include "garnet/lib/measure/test_events.h"
#include "gtest/gtest.h"

namespace tracing {
namespace measure {
namespace {

TEST(MeasureArgumentValueTest, ArgumentValue) {
  std::vector<ArgumentValueSpec> specs = {ArgumentValueSpec(
      {42u, {"event_foo", "category_bar"}, "arg_foo", "unit_bar"})};

  MeasureArgumentValue measure(std::move(specs));

  fbl::Vector<trace::Argument> arguments;
  arguments.push_back(
      trace::Argument("arg_foo", trace::ArgumentValue::MakeUint64(149)));
  measure.Process(
      test::Instant("event_foo", "category_bar", 10u, std::move(arguments)));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({149u}), results[42u]);
}

TEST(MeasureArgumentValueTest, ArgumentValueDoesNotMatchSpec) {
  std::vector<ArgumentValueSpec> specs = {ArgumentValueSpec(
      {42u, {"event_foo", "category_bar"}, "arg_foo", "unit_bar"})};

  MeasureArgumentValue measure(std::move(specs));

  fbl::Vector<trace::Argument> arguments;
  arguments.push_back(
      trace::Argument("bytes", trace::ArgumentValue::MakeUint64(149)));
  measure.Process(
      test::Instant("event_baz", "category_bar", 10u, std::move(arguments)));

  auto results = measure.results();
  EXPECT_EQ(0u, results.size());
}

TEST(MeasureArgumentValueTest, ArgumentValueArgumentNotFound) {
  std::vector<ArgumentValueSpec> specs = {
      ArgumentValueSpec({42u, {"event_foo", "category_bar"}, "arg", "bytes"})};

  MeasureArgumentValue measure(std::move(specs));

  fbl::Vector<trace::Argument> arguments;
  // Right argument type, wrong argument name.
  arguments.push_back(
      trace::Argument("foo", trace::ArgumentValue::MakeUint64(149)));
  // Right argument name, wrong argument type.
  arguments.push_back(
      trace::Argument("arg", trace::ArgumentValue::MakeDouble(149)));
  measure.Process(
      test::Instant("event_foo", "category_bar", 10u, std::move(arguments)));

  auto results = measure.results();
  EXPECT_EQ(0u, results.size());
}

}  // namespace

}  // namespace measure
}  // namespace tracing
