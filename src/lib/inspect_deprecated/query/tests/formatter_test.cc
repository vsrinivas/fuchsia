// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect_deprecated/query/formatter.h"

#include <lib/inspect_deprecated/query/json_formatter.h>
#include <lib/inspect_deprecated/query/text_formatter.h>

#include "lib/inspect_deprecated/health/health.h"
#include "lib/inspect_deprecated/hierarchy.h"
#include "lib/inspect_deprecated/query/source.h"
#include "lib/inspect_deprecated/testing/inspect.h"

using namespace inspect_deprecated::testing;
using namespace inspect_deprecated::hierarchy;

namespace {

inspect_deprecated::Source MakeSourceFromHierarchy(inspect_deprecated::ObjectHierarchy hierarchy) {
  inspect_deprecated::Location location = {
      .directory_path = "./hub/",
      .file_name = "root.inspect",
      .inspect_path_components = {"child", "node"},
      .type = inspect_deprecated::Location::Type::INSPECT_FILE_FORMAT,
  };

  return inspect_deprecated::Source(std::move(location), std::move(hierarchy));
}

inspect_deprecated::Source MakeTestSource() {
  inspect_deprecated::ObjectHierarchy hierarchy;

  {
    Node node;
    node.name() = "node";
    node.metrics().emplace_back(Metric("int", IntMetric(-2)));
    node.metrics().emplace_back(Metric("uint", UIntMetric(2)));
    node.metrics().emplace_back(Metric("double", DoubleMetric(1.25)));
    node.metrics().emplace_back(Metric("int_array", IntArray({1, 2, 3}, ArrayDisplayFormat::FLAT)));
    node.properties().emplace_back(Property("string", StringProperty("value")));
    hierarchy.node() = std::move(node);
  }

  {
    Node child;
    child.name() = "node_child";
    child.metrics().emplace_back(Metric("child_int", IntMetric(-5)));
    auto& child_hierarchy = hierarchy.children().emplace_back(
        inspect_deprecated::ObjectHierarchy(std::move(child), {}));

    Node child_health;
    child_health.name() = inspect_deprecated::kHealthNodeName;
    child_health.properties().emplace_back(
        Property("status", StringProperty(inspect_deprecated::kHealthUnhealthy)));
    child_health.properties().emplace_back(
        Property("message", StringProperty("Some health error")));

    child_hierarchy.children().emplace_back(
        inspect_deprecated::ObjectHierarchy(std::move(child_health), {}));
  }

  {
    Node health;
    health.name() = inspect_deprecated::kHealthNodeName;
    health.properties().emplace_back(
        Property("status", StringProperty(inspect_deprecated::kHealthOk)));

    hierarchy.children().emplace_back(inspect_deprecated::ObjectHierarchy(std::move(health), {}));
  }

  return MakeSourceFromHierarchy(std::move(hierarchy));
}

// Test that basic formatting works in JSON and Text for a small hierarchy.
TEST(Formatter, PrintHierarchy) {
  std::vector<inspect_deprecated::Source> sources;
  sources.emplace_back(MakeTestSource());

  inspect_deprecated::TextFormatter text_format(
      inspect_deprecated::TextFormatter::Options{.indent = 2},
      inspect_deprecated::Formatter::PathFormat::NONE);
  inspect_deprecated::JsonFormatter json_format(
      inspect_deprecated::JsonFormatter::Options{.indent = 2},
      inspect_deprecated::Formatter::PathFormat::NONE);
  inspect_deprecated::JsonFormatter json_format_no_indent(
      inspect_deprecated::JsonFormatter::Options{.indent = 0},
      inspect_deprecated::Formatter::PathFormat::NONE);

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
  inspect_deprecated::TextFormatter text_formatter({.indent = 2},
                                                   inspect_deprecated::Formatter::PathFormat::FULL);
  inspect_deprecated::JsonFormatter json_formatter({.indent = 2},
                                                   inspect_deprecated::Formatter::PathFormat::FULL);

  std::vector<inspect_deprecated::Source> sources;
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
  inspect_deprecated::TextFormatter text_formatter({.indent = 2},
                                                   inspect_deprecated::Formatter::PathFormat::FULL);
  inspect_deprecated::JsonFormatter json_formatter({.indent = 2},
                                                   inspect_deprecated::Formatter::PathFormat::FULL);

  std::vector<inspect_deprecated::Source> sources;
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
  std::vector<inspect_deprecated::Source> sources;
  sources.emplace_back(MakeTestSource());

  // Text.
  inspect_deprecated::TextFormatter text_formatter({.indent = 2},
                                                   inspect_deprecated::Formatter::PathFormat::FULL);
  EXPECT_EQ(text_formatter.FormatHealth(sources),
            R"(./hub/root.inspect#child/node = OK
./hub/root.inspect#child/node/node_child = UNHEALTHY (Some health error)
)");

  // Indented json.
  inspect_deprecated::JsonFormatter json_formatter({.indent = 2},
                                                   inspect_deprecated::Formatter::PathFormat::FULL);
  EXPECT_EQ(json_formatter.FormatHealth(sources),
            R"({
  "./hub/root.inspect#child/node": {
    "status": "OK"
  },
  "./hub/root.inspect#child/node/node_child": {
    "status": "UNHEALTHY",
    "message": "Some health error"
  }
})");

  // Non-indented json.
  inspect_deprecated::JsonFormatter json_formatter_no_indent(
      {.indent = 0}, inspect_deprecated::Formatter::PathFormat::FULL);
  EXPECT_EQ(
      json_formatter_no_indent.FormatHealth(sources),
      R"({"./hub/root.inspect#child/node":{"status":"OK"},"./hub/root.inspect#child/node/node_child":{"status":"UNHEALTHY","message":"Some health error"}})");
}

}  // namespace
