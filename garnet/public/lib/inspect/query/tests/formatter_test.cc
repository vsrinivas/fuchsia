// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/query/formatter.h"

#include <lib/inspect/query/json_formatter.h>
#include <lib/inspect/query/text_formatter.h>

#include "lib/inspect/health/health.h"
#include "lib/inspect/hierarchy.h"
#include "lib/inspect/query/source.h"
#include "lib/inspect/testing/inspect.h"

using namespace inspect::testing;
using namespace inspect::hierarchy;

namespace {

inspect::Source MakeSourceFromHierarchy(inspect::ObjectHierarchy hierarchy) {
  inspect::Location location = {
      .directory_path = "./hub/",
      .file_name = "root.inspect",
      .inspect_path_components = {"child", "node"},
      .type = inspect::Location::Type::INSPECT_VMO,
  };

  return inspect::Source(std::move(location), std::move(hierarchy));
}

inspect::Source MakeTestSource() {
  inspect::ObjectHierarchy hierarchy;

  {
    Node node;
    node.name() = "node";
    node.metrics().emplace_back(Metric("int", IntMetric(-2)));
    node.metrics().emplace_back(Metric("uint", UIntMetric(2)));
    node.metrics().emplace_back(Metric("double", DoubleMetric(1.25)));
    node.metrics().emplace_back(
        Metric("int_array", IntArray({1, 2, 3}, ArrayDisplayFormat::FLAT)));
    node.properties().emplace_back(Property("string", StringProperty("value")));
    hierarchy.node() = std::move(node);
  }

  {
    Node child;
    child.name() = "node_child";
    child.metrics().emplace_back(Metric("child_int", IntMetric(-5)));
    auto& child_hierarchy = hierarchy.children().emplace_back(
        inspect::ObjectHierarchy(std::move(child), {}));

    Node child_health;
    child_health.name() = inspect::kHealthNodeName;
    child_health.properties().emplace_back(
        Property("status", StringProperty(inspect::kHealthUnhealthy)));
    child_health.properties().emplace_back(
        Property("message", StringProperty("Some health error")));

    child_hierarchy.children().emplace_back(
        inspect::ObjectHierarchy(std::move(child_health), {}));
  }

  {
    Node health;
    health.name() = inspect::kHealthNodeName;
    health.properties().emplace_back(
        Property("status", StringProperty(inspect::kHealthOk)));

    hierarchy.children().emplace_back(
        inspect::ObjectHierarchy(std::move(health), {}));
  }

  return MakeSourceFromHierarchy(std::move(hierarchy));
}

// Test that basic formatting works in JSON and Text for a small hierarchy.
TEST(Formatter, PrintHierarchy) {
  std::vector<inspect::Source> sources;
  sources.emplace_back(MakeTestSource());

  inspect::TextFormatter text_format(
      inspect::TextFormatter::Options{.indent = 2},
      inspect::Formatter::PathFormat::NONE);
  inspect::JsonFormatter json_format(
      inspect::JsonFormatter::Options{.indent = 2},
      inspect::Formatter::PathFormat::NONE);
  inspect::JsonFormatter json_format_no_indent(
      inspect::JsonFormatter::Options{.indent = 0},
      inspect::Formatter::PathFormat::NONE);

  std::vector<std::string> result = {
      text_format.FormatSourcesRecursive(sources),
      json_format.FormatSourcesRecursive(sources),
      json_format_no_indent.FormatSourcesRecursive(sources),
  };

  EXPECT_EQ(text_format.FormatSourcesRecursive(sources),
            R"(node:
  string = value
  int = -2
  uint = 2
  double = 1.250000
  int_array = [1, 2, 3]
  node_child:
    child_int = -5
    fuchsia.inspect.Health:
      status = UNHEALTHY
      message = Some health error
  fuchsia.inspect.Health:
    status = OK
)");

  EXPECT_EQ(json_format.FormatSourcesRecursive(sources),
            R"([
  {
    "path": "./hub/root.inspect#child/node",
    "contents": {
      "node": {
        "string": "value",
        "int": -2,
        "uint": 2,
        "double": 1.25,
        "int_array": [
          1,
          2,
          3
        ],
        "node_child": {
          "child_int": -5,
          "fuchsia.inspect.Health": {
            "status": "UNHEALTHY",
            "message": "Some health error"
          }
        },
        "fuchsia.inspect.Health": {
          "status": "OK"
        }
      }
    }
  }
])");

  EXPECT_EQ(
      json_format_no_indent.FormatSourcesRecursive(sources),
      R"([{"path":"./hub/root.inspect#child/node","contents":{"node":{"string":"value","int":-2,"uint":2,"double":1.25,"int_array":[1,2,3],"node_child":{"child_int":-5,"fuchsia.inspect.Health":{"status":"UNHEALTHY","message":"Some health error"}},"fuchsia.inspect.Health":{"status":"OK"}}}}])");
}

TEST(Formatter, PrintListing) {
  inspect::TextFormatter text_formatter({.indent = 2},
                                        inspect::Formatter::PathFormat::FULL);
  inspect::JsonFormatter json_formatter({.indent = 2},
                                        inspect::Formatter::PathFormat::FULL);

  std::vector<inspect::Source> sources;
  sources.emplace_back(MakeTestSource());

  EXPECT_EQ(text_formatter.FormatChildListing(sources),
            "./hub/root.inspect#child/node/node_child\n"
            "./hub/root.inspect#child/node/fuchsia.inspect.Health\n");
  EXPECT_EQ(json_formatter.FormatChildListing(sources),
            R"([
  "./hub/root.inspect#child/node/node_child",
  "./hub/root.inspect#child/node/fuchsia.inspect.Health"
])");
}

TEST(Formatter, PrintFind) {
  inspect::TextFormatter text_formatter({.indent = 2},
                                        inspect::Formatter::PathFormat::FULL);
  inspect::JsonFormatter json_formatter({.indent = 2},
                                        inspect::Formatter::PathFormat::FULL);

  std::vector<inspect::Source> sources;
  sources.emplace_back(MakeTestSource());

  EXPECT_EQ(text_formatter.FormatSourceLocations(sources),
            "./hub/root.inspect#child/node\n"
            "./hub/root.inspect#child/node/node_child\n"
            "./hub/root.inspect#child/node/node_child/fuchsia.inspect.Health\n"
            "./hub/root.inspect#child/node/fuchsia.inspect.Health\n");
  EXPECT_EQ(json_formatter.FormatSourceLocations(sources),
            R"([
  "./hub/root.inspect#child/node",
  "./hub/root.inspect#child/node/node_child",
  "./hub/root.inspect#child/node/node_child/fuchsia.inspect.Health",
  "./hub/root.inspect#child/node/fuchsia.inspect.Health"
])");
}

TEST(Formatter, Health) {
  // TODO(donosoc): Test json formatter.
  inspect::TextFormatter text_formatter({.indent = 2},
                                        inspect::Formatter::PathFormat::FULL);
  std::vector<inspect::Source> sources;
  sources.emplace_back(MakeTestSource());
  EXPECT_EQ(text_formatter.FormatHealth(sources),
            R"(./hub/root.inspect#child/node = OK
./hub/root.inspect#child/node/node_child = UNHEALTHY (Some health error)
)");
}

}  // namespace
