// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/scene_dumper.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gtest/gtest-matchers.h"
#include "sdk/lib/syslog/cpp/macros.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/allocation/id.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

using allocation::ImageMetadata;
using allocation::kInvalidImageId;
using flatland::ImageRect;

namespace {

// Ignored lines at the start of the dump containing user-readable formatting but no relevant
// information for testing.
constexpr int kIgnoredLinesAtStartOfDump = 3;

// Instance topologies are dumped on the same line containing this token.
constexpr char kInstanceDumpLineIdentifierToken[] = "Instance";

// Images are dumped on the same line containing this token.
constexpr char kImageDumpLineIdentifierToken[] = "image:";

constexpr flatland::TransformHandle::InstanceId kLinkInstanceId = 0;

// Creates a link in |links| to the the graph rooted at |instance_id:0|.
void MakeLink(flatland::GlobalTopologyData::LinkTopologyMap& links, uint64_t instance_id) {
  links[{kLinkInstanceId, instance_id}] = {instance_id, 0};
}

// Returns lines from a scene dump input stream. Ignores |kIgnoredLinesAtStartOfDump| lines at the
// start of the line. The returned lines begin at the root topology node.
std::vector<std::string> GetLines(std::istream& input) {
  std::vector<std::string> lines;
  std::string line;
  auto ignored_lines = 0;
  while (std::getline(input, line)) {
    if (++ignored_lines > kIgnoredLinesAtStartOfDump) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::string NodeStr(flatland::TransformHandle& node) {
  std::ostringstream output;
  output << node.GetInstanceId() << ":" << node.GetTransformId();
  return output.str();
}

std::string ImageStr(ImageMetadata image) {
  std::ostringstream output;
  output << image;
  return output.str();
}

std::string RectStr(ImageRect rect) {
  std::ostringstream output;
  output << rect;
  return output.str();
}

// The topology dump of the scene processes the scene such that each transform node is on its own
// line, with children node indented and sibling nodes at the same indentation level. For example,
// assuming, A is the root node, B and C are direct children, and D, E, F and G, H are children of B
// and C -- the output (ignoring further formatting other than indentation) appears as the
// following:
// A
//     B
//         D
//         E
//     C
//         G
//         H
//
// Any debug names for a particular node appears above the node (on a separate line) with the same
// indentation as the node. For instance:
// A
//     Node_B_Name
//     B
//     C
//
// The following helper functions test depth level (i.e. A has depth of 1; B and C have depth of 2;
// D, E, G, and H have depth of 3).

// Expect a transform handle to be dumped at the specified line. Returns the position, within the
// line, of where the transform handle begins.
size_t ExpectNodeLineNumberAndGetIndex(flatland::TransformHandle& node, size_t node_line_number,
                                       const std::vector<std::string>& line_dump) {
  std::string node_name = NodeStr(node);
  auto node_index = line_dump[node_line_number].find(node_name);
  EXPECT_NE(node_index, std::string::npos);
  return node_index;
}

// Expect a transform handle debug name at the specified line. Returns the position, within the
// line, of where the debug name begins.
size_t ExpectNameLineNumberAndGetIndex(const std::string& name, size_t name_line_number,
                                       const std::vector<std::string>& line_dump) {
  auto name_index = line_dump[name_line_number].find(name);
  EXPECT_NE(name_index, std::string::npos);
  return name_index;
}

// Expect topology node A to have less depth level than topology node B. I.e. node A is closer to
// (or is) the root node than topology node B.
void ExpectTopologyNodeHasLessDepthLevel(flatland::TransformHandle node_a,
                                         size_t node_a_line_number,
                                         flatland::TransformHandle node_b,
                                         size_t node_b_line_number,
                                         const std::vector<std::string>& line_dump) {
  ASSERT_LT(node_a_line_number, line_dump.size());
  ASSERT_LT(node_b_line_number, line_dump.size());
  const auto node_a_index = ExpectNodeLineNumberAndGetIndex(node_a, node_a_line_number, line_dump);
  const auto node_b_index = ExpectNodeLineNumberAndGetIndex(node_b, node_b_line_number, line_dump);
  EXPECT_LT(node_a_index, node_b_index);
}

// Expect topology node A and topology node B to have the same depth level. I.e. node A and node B
// are the same number of 'hops' from the root node.
void ExpectTopologyNodeHasSameDepthLevel(flatland::TransformHandle node_a,
                                         size_t node_a_line_number,
                                         flatland::TransformHandle node_b,
                                         size_t node_b_line_number,
                                         const std::vector<std::string>& line_dump) {
  ASSERT_LT(node_a_line_number, line_dump.size());
  ASSERT_LT(node_b_line_number, line_dump.size());
  const auto node_a_index = ExpectNodeLineNumberAndGetIndex(node_a, node_a_line_number, line_dump);
  const auto node_b_index = ExpectNodeLineNumberAndGetIndex(node_b, node_b_line_number, line_dump);
  EXPECT_EQ(node_a_index, node_b_index);
}

// Expect the specified node to be dumped with the specified name printed above.
void ExpectNodeName(flatland::TransformHandle node, size_t node_line_number,
                    const std::string& name, const std::vector<std::string>& line_dump) {
  ASSERT_LT(node_line_number, line_dump.size());
  const auto node_index = ExpectNodeLineNumberAndGetIndex(node, node_line_number, line_dump);
  const auto name_index = ExpectNameLineNumberAndGetIndex(
      std::string("(") + name + std::string(")"), node_line_number, line_dump);
  EXPECT_GT(name_index, node_index);  // Name appears to right of node.
}

// Find the line number containing an instance dump (and also |kInstanceDumpLineIdentifierToken|).
// Returns the line number of the following line (after the found instance). This can then be
// specified as |beginning_at| to find subsequent instances.
size_t FindInstanceDumpLineNumber(const std::vector<std::string>& line_dump,
                                  flatland::TransformHandle::InstanceId instance_id) {
  for (size_t i = 0; i < line_dump.size(); i++) {
    if (line_dump[i].find(std::string(kInstanceDumpLineIdentifierToken) + " " +
                          std::to_string(instance_id) + " ") == 0) {
      return i;
    }
  }
  return -1;
}

// Checks that the total number of instances dumped (and |kInstanceDumpLineIdentifierToken|) matches
// the expectation.
void ExpectInstanceDumpCount(const std::vector<std::string>& line_dump, int expected_count) {
  int count = 0;
  for (auto& line : line_dump) {
    if (line.find(kInstanceDumpLineIdentifierToken) == 0) {
      count++;
    }
  }
  EXPECT_EQ(count, expected_count);
}

// Sets expectations that the instance is dumped alongside its associated topology.
// Returns the line number of the line following the instance dump.
void ExpectInstanceDump(flatland::TransformHandle::InstanceId instance_id, const std::string& name,
                        const std::vector<std::string>& line_dump) {
  auto line_number = FindInstanceDumpLineNumber(line_dump, instance_id);
  ASSERT_LT(line_number, line_dump.size());
  auto instance_str = std::string(" (") + name + std::string(")");
  ExpectNameLineNumberAndGetIndex(instance_str, line_number, line_dump);
}

// Find the line number containing an image dump (and also |kImageDumpLineIdentifierToken|). Returns
// the line number of the following line (after the found image). This can then be specified as
// |beginning_at| to find subsequent images.
size_t FindImageDumpLineNumber(const std::vector<std::string>& line_dump, size_t beginning_at = 0) {
  for (size_t i = beginning_at; i < line_dump.size(); i++) {
    if (line_dump[i].find(kImageDumpLineIdentifierToken) != std::string::npos) {
      return i;
    }
  }
  return -1;
}

// Checks that the total number of images dumped (and |kImageDumpLineIdentifierToken|) matches the
// expectation.
void ExpectImageDumpCount(const std::vector<std::string>& line_dump, int expected_count) {
  int count = 0;
  for (auto& line : line_dump) {
    if (line.find(kImageDumpLineIdentifierToken) != std::string::npos) {
      count++;
    }
  }
  EXPECT_EQ(count, expected_count);
}

// Sets expectations that the image is dumped alongside its associated transform and image rect.
// Returns the line number of the line following the image dump. This can then be used to specify
// |beginning_at| to check subsequent image dumps.
size_t ExpectImageDump(ImageMetadata image, flatland::TransformHandle node, ImageRect rect,
                       const std::vector<std::string>& line_dump, size_t beginning_at = 0) {
  auto line_number = FindImageDumpLineNumber(line_dump, beginning_at);
  EXPECT_LE(line_number, (size_t)-1);
  auto index = line_dump[line_number++].find(ImageStr(image));
  EXPECT_NE(index, std::string::npos);
  index = line_dump[line_number++].find(NodeStr(node));
  EXPECT_NE(index, std::string::npos);
  index = line_dump[line_number++].find(RectStr(rect));
  EXPECT_NE(index, std::string::npos);
  return line_number;
}

}  // namespace

namespace flatland::test {

TEST(SceneDumperTest, TopologyTree) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {{0, 2}, 0}, {{0, 5}, 0}},  // 1:0 - 0:5
                                                //    \
                                                //     0:2
                                                //
      {{{2, 0}, 2}, {{0, 3}, 0}, {{0, 4}, 0}},  // 2:0 - 0:4
                                                //    \
                                                //     0:3
                                                //
      {{{3, 0}, 0}},                            // 3:0
      {{{4, 0}, 0}},                            // 4:0
      {{{5, 0}, 0}}                             // 5:0
  };

  for (const auto& v : vectors) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    uber_structs[v[0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0
  MakeLink(links, 4);  // 0:4 - 4:0
  MakeLink(links, 5);  // 0:5 - 5:0

  GlobalImageVector images;
  GlobalIndexVector image_indices;
  GlobalRectangleVector image_rectangles;

  std::stringstream output;

  auto topology_data =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, 0, {1, 0});

  DumpScene(uber_structs, topology_data, images, image_indices, image_rectangles, output);
  auto lines = GetLines(output);

  // {1, 0} is the root with {2, 0} on the next line as child.
  ExpectTopologyNodeHasLessDepthLevel({1, 0}, 0, {2, 0}, 1, lines);
  // {2, 0} has two children - {3, 0} and {4, 0}.
  ExpectTopologyNodeHasLessDepthLevel({2, 0}, 1, {3, 0}, 2, lines);
  ExpectTopologyNodeHasSameDepthLevel({3, 0}, 2, {4, 0}, 3, lines);
  // {5, 0} is direct child of {1, 0} and sibling of {2, 0}.
  ExpectTopologyNodeHasLessDepthLevel({1, 0}, 0, {5, 0}, 4, lines);
  ExpectTopologyNodeHasSameDepthLevel({2, 0}, 1, {5, 0}, 4, lines);

  ExpectInstanceDumpCount(lines, 5);
  ExpectInstanceDump(1, "", lines);
  ExpectInstanceDump(2, "", lines);
  ExpectInstanceDump(3, "", lines);
  ExpectInstanceDump(4, "", lines);
  ExpectInstanceDump(5, "", lines);
}

TEST(SceneDumperTest, TopologyTreeDeep) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {{0, 2}, 0}, {{0, 6}, 0}},  // 1:0 - 0:2
      {{{2, 0}, 1}, {{0, 3}, 0}},               // 2:0 - 0:3
      {{{3, 0}, 1}, {{0, 4}, 0}},               // 3:0 - 4:0
      {{{4, 0}, 1}, {{0, 5}, 0}},               // 4:0 - 5:0
      {{{5, 0}, 0}},                            // 5:0
      {{{6, 0}, 0}}                             // 6:0
  };

  for (const auto& v : vectors) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    uber_structs[v[0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0
  MakeLink(links, 4);  // 0:4 - 4:0
  MakeLink(links, 5);  // 0:5 - 5:0
  MakeLink(links, 6);  // 0:5 - 5:0

  GlobalImageVector images;
  GlobalIndexVector image_indices;
  GlobalRectangleVector image_rectangles;

  std::stringstream output;

  auto topology_data =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, 0, {1, 0});

  DumpScene(uber_structs, topology_data, images, image_indices, image_rectangles, output);
  auto lines = GetLines(output);

  ExpectTopologyNodeHasLessDepthLevel({1, 0}, 0, {2, 0}, 1, lines);
  ExpectTopologyNodeHasLessDepthLevel({2, 0}, 1, {3, 0}, 2, lines);
  ExpectTopologyNodeHasLessDepthLevel({3, 0}, 2, {4, 0}, 3, lines);
  ExpectTopologyNodeHasLessDepthLevel({4, 0}, 3, {5, 0}, 4, lines);
  ExpectTopologyNodeHasSameDepthLevel({2, 0}, 1, {6, 0}, 5, lines);

  ExpectInstanceDumpCount(lines, 6);
  ExpectInstanceDump(1, "", lines);
  ExpectInstanceDump(2, "", lines);
  ExpectInstanceDump(3, "", lines);
  ExpectInstanceDump(4, "", lines);
  ExpectInstanceDump(5, "", lines);
  ExpectInstanceDump(6, "", lines);
}

TEST(SceneDumperTest, TopologyTreeWithNames) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {{0, 2}, 0}, {{0, 5}, 0}},  // 1:0 - 0:5
                                                //    \
                                                //     0:2
                                                //
      {{{2, 0}, 2}, {{0, 3}, 0}, {{0, 4}, 0}},  // 2:0 - 0:4
                                                //    \
                                                //     0:3
                                                //
      {{{3, 0}, 0}},                            // 3:0
      {{{4, 0}, 0}},                            // 4:0
      {{{5, 0}, 0}}                             // 5:0
  };

  const std::string names[] = {"", "2_0_ABC", "3_0_DEF", "", "5_0_GHI"};

  for (int i = 0; i < 5; i++) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[i];
    uber_struct->debug_name = names[i];
    uber_structs[vectors[i][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0
  MakeLink(links, 4);  // 0:4 - 4:0
  MakeLink(links, 5);  // 0:5 - 5:0

  GlobalImageVector images;
  GlobalIndexVector image_indices;
  GlobalRectangleVector image_rectangles;

  std::stringstream output;

  auto topology_data =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, 0, {1, 0});

  DumpScene(uber_structs, topology_data, images, image_indices, image_rectangles, output);
  auto lines = GetLines(output);

  // {1, 0} is the root with {2, 0} as a child node.
  ExpectTopologyNodeHasLessDepthLevel({1, 0}, 0, {2, 0}, 1, lines);
  // {2, 0} has two children - {3, 0} and {4, 0}.
  ExpectTopologyNodeHasLessDepthLevel({2, 0}, 1, {3, 0}, 2, lines);
  ExpectTopologyNodeHasSameDepthLevel({3, 0}, 2, {4, 0}, 3, lines);
  // {5, 0} is direct child of {1, 0} and sibling of {2, 0}.
  ExpectTopologyNodeHasLessDepthLevel({1, 0}, 0, {5, 0}, 4, lines);
  ExpectTopologyNodeHasSameDepthLevel({2, 0}, 1, {5, 0}, 4, lines);

  ExpectNodeName({2, 0}, 1, names[1], lines);
  ExpectNodeName({3, 0}, 2, names[2], lines);
  ExpectNodeName({5, 0}, 4, names[4], lines);

  ExpectInstanceDumpCount(lines, 5);
  ExpectInstanceDump(1, names[0], lines);
  ExpectInstanceDump(2, names[1], lines);
  ExpectInstanceDump(3, names[2], lines);
  ExpectInstanceDump(4, names[3], lines);
  ExpectInstanceDump(5, names[4], lines);
}

TEST(SceneDumperTest, ImageRectangleMetadata) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {{0, 2}, 0}, {{0, 3}, 0}},  // 1:0 - 0:3
                                                //    \
                                                //     0:2
                                                //
      {{{2, 0}, 0}},                            // 2:0
      {{{3, 0}, 0}},                            // 3:0
  };

  {
    auto& v = vectors[0];
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    uber_structs[v[0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto& v = vectors[1];
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    ImageMetadata image;
    image.width = 800;
    image.height = 600;
    image.identifier = 1;
    uber_struct->images.insert(std::make_pair(v[0].handle, image));
    uber_structs[v[0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto& v = vectors[2];
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    ImageMetadata image;
    image.width = 300;
    image.height = 400;
    image.identifier = kInvalidImageId;
    image.multiply_color = {.2f, .4f, .8f, 1.f};
    uber_struct->images.insert(std::make_pair(v[0].handle, image));
    uber_structs[v[0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0

  std::stringstream output;

  auto topology_data =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, 0, {1, 0});
  auto [image_indices, images] = ComputeGlobalImageData(topology_data.topology_vector,
                                                        topology_data.parent_indices, uber_structs);

  GlobalRectangleVector image_rectangles;
  image_rectangles.push_back(ImageRect({50, 60}, {200, 300}));
  image_rectangles.push_back(ImageRect({90, 100}, {400, 500}));

  DumpScene(uber_structs, topology_data, images, image_indices, image_rectangles, output);
  auto lines = GetLines(output);

  // {1, 0} is the root with two child transforms {2, 0} and {3, 0}.
  ExpectTopologyNodeHasLessDepthLevel({1, 0}, 0, {2, 0}, 1, lines);
  ExpectTopologyNodeHasLessDepthLevel({1, 0}, 0, {3, 0}, 2, lines);
  ExpectTopologyNodeHasSameDepthLevel({2, 0}, 1, {3, 0}, 2, lines);

  ExpectInstanceDumpCount(lines, 3);
  ExpectInstanceDump(1, "", lines);
  ExpectInstanceDump(2, "", lines);
  ExpectInstanceDump(3, "", lines);

  ExpectImageDumpCount(lines, 2);
  // First image dump.
  const auto& node = vectors[1][0].handle;
  const auto& uber_struct = uber_structs[node.GetInstanceId()];
  const auto& image = uber_struct->images.find(node)->second;
  size_t next_image_dump_line_number = ExpectImageDump(image, node, image_rectangles[0], lines);
  // Second image dump.
  const auto& second_node = vectors[2][0].handle;
  const auto& second_uber_struct = uber_structs[second_node.GetInstanceId()];
  const auto& second_image = second_uber_struct->images.find(second_node)->second;
  next_image_dump_line_number = ExpectImageDump(second_image, second_node, image_rectangles[1],
                                                lines, next_image_dump_line_number);
  EXPECT_EQ(FindImageDumpLineNumber(lines, next_image_dump_line_number), (size_t)-1);
}

}  // namespace flatland::test
