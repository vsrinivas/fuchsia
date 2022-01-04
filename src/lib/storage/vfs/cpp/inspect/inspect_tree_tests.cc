// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains tests for the InspectTree class as well as the associated inspect data.

#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/zx/status.h>

#include <optional>

#include <gtest/gtest.h>

#include "src/lib/storage/vfs/cpp/inspect/inspect_tree.h"

using namespace ::testing;
using namespace inspect::testing;

namespace fs_inspect {

namespace {

// Helper functions to create validators that can be used to validate an inspect::Hierarchy against
// the expected node name, properties, and values.

// Ensures that the basic required nodes exist within the filesystem hierarchy. Properties within
// these nodes are not validated (use the matchers below to match specific properties).
::testing::Matcher<const inspect::Hierarchy&> HasExpectedNodeLayout() {
  return ChildrenMatch(IsSupersetOf({NodeMatches(NameMatches(kInfoNodeName)),
                                     NodeMatches(NameMatches(kUsageNodeName)),
                                     NodeMatches(NameMatches(kVolumeNodeName))}));
}

// Match the given hierarchy against fs.info using the given InfoData values.
::testing::Matcher<const inspect::Hierarchy&> NodePropertiesMatch(const InfoData& info) {
  return NodeMatches(
      AllOf(NameMatches(kInfoNodeName),
            PropertyList(UnorderedElementsAre(
                UintIs(InfoData::kPropId, info.id), UintIs(InfoData::kPropType, info.type),
                StringIs(InfoData::kPropName, info.name),
                UintIs(InfoData::kPropVersionMajor, info.version_major),
                UintIs(InfoData::kPropVersionMinor, info.version_minor),
                UintIs(InfoData::kPropOldestMinorVersion, info.oldest_minor_version),
                UintIs(InfoData::kPropBlockSize, info.block_size),
                UintIs(InfoData::kPropMaxFilenameLength, info.max_filename_length)))));
}

// Match the given hierarchy against fs.usage using the given UsageData values.
::testing::Matcher<const inspect::Hierarchy&> NodePropertiesMatch(const UsageData& usage) {
  return NodeMatches(AllOf(
      NameMatches(kUsageNodeName),
      PropertyList(UnorderedElementsAre(UintIs(UsageData::kPropTotalBytes, usage.total_bytes),
                                        UintIs(UsageData::kPropUsedBytes, usage.used_bytes),
                                        UintIs(UsageData::kPropTotalNodes, usage.total_nodes),
                                        UintIs(UsageData::kPropUsedNodes, usage.used_nodes)))));
}

// Match the given hierarchy against fs.volume using the given VolumeData values.
::testing::Matcher<const inspect::Hierarchy&> NodePropertiesMatch(const VolumeData& volume) {
  return NodeMatches(AllOf(
      NameMatches(kVolumeNodeName),
      PropertyList(UnorderedElementsAre(
          UintIs(VolumeData::kPropSizeBytes, volume.size_info.size_bytes),
          UintIs(VolumeData::kPropSizeLimitBytes, volume.size_info.size_limit_bytes),
          UintIs(VolumeData::kPropAvailableSpaceBytes, volume.size_info.available_space_bytes),
          UintIs(VolumeData::kPropOutOfSpaceEvents, volume.out_of_space_events)))));
}

}  // namespace

// Fake implementation of a filesystem inspect tree for testing purposes. Encapsulates structured
// data and an `fs_inspect::InspectTree` class similar to real filesystems, but with additional
// helpers to support tests.
class FakeInspectTree {
 public:
  explicit FakeInspectTree(inspect::LazyNodeCallbackFn detail_node = nullptr)
      : fs_inspect_nodes_{
            CreateTree(inspector_.GetRoot(),
                       NodeCallbacks{.info_callback = [this]() { return info_data_; },
                                     .usage_callback = [this]() { return usage_data_; },
                                     .volume_callback = [this]() { return volume_data_; },
                                     .detail_node_callback = std::move(detail_node)})} {}

  // Setters to modify the data the tree exposes.
  void SetInfoData(const InfoData& info_data) { info_data_ = info_data; }
  void SetUsageData(const UsageData& usage_data) { usage_data_ = usage_data; }
  void SetVolumeData(const VolumeData& volume_data) { volume_data_ = volume_data; }

  // Pointers to nodes within the hierarchy obtained from the last call to `UpdateSnapshot`.
  // Invalidated every time UpdateSnapshot() is called.
  const inspect::Hierarchy* RootNode() const { return &snapshot_; }
  const inspect::Hierarchy* InfoNode() const { return snapshot_.GetByPath({kInfoNodeName}); }
  const inspect::Hierarchy* UsageNode() const { return snapshot_.GetByPath({kUsageNodeName}); }
  const inspect::Hierarchy* VolumeNode() const { return snapshot_.GetByPath({kVolumeNodeName}); }
  const inspect::Hierarchy* DetailNode() const { return snapshot_.GetByPath({kDetailNodeName}); }

  // Updates the exposed node hierarchy by taking a snapshot of the tree and storing it internally.
  // Invalidates any node/hierarchy pointers previously obtained from this object.
  void UpdateSnapshot() {
    auto snapshot = fpromise::run_single_threaded(inspect::ReadFromInspector(inspector_));
    ZX_ASSERT(snapshot.is_ok());
    snapshot_ = std::move(snapshot.value());
  }

 private:
  inspect::Inspector inspector_ = {};

  // Structured data to be exposed in the inspect hierarchy.
  fs_inspect::InfoData info_data_ = {};
  fs_inspect::UsageData usage_data_ = {};
  fs_inspect::VolumeData volume_data_ = {};

  // Last snapshot taken of the inspect tree.
  inspect::Hierarchy snapshot_ = {};

  // `fs_inspect_nodes_` should be last as it owns callbacks that reference other data in this
  // object.
  fs_inspect::FilesystemNodes fs_inspect_nodes_;
};

// Validates that the root node contains children named "fs.info", "fs.usage", and "fs.volume".
TEST(VfsInspectTree, ValidateNodeHierarchy) {
  FakeInspectTree tree;
  tree.UpdateSnapshot();
  // Ensure that the tree matches the expected node layout.
  ASSERT_NE(tree.RootNode(), nullptr);
  ASSERT_THAT(*tree.RootNode(), HasExpectedNodeLayout());

  // Ensure that pointers to all common nodes are now valid as well.
  EXPECT_NE(tree.InfoNode(), nullptr);
  EXPECT_NE(tree.UsageNode(), nullptr);
  EXPECT_NE(tree.VolumeNode(), nullptr);
  // The detail node should not exist since we did not provide a callback to populate it.
  EXPECT_EQ(tree.DetailNode(), nullptr);
}

// Same as ValidateNodeHierarchy, but also checks "fs.detail" and validates the attached properties.
TEST(VfsInspectTree, AttachDetailNode) {
  // Create another tree but with an fs.detail node this time.
  auto make_detail = []() {
    inspect::Inspector insp;
    insp.GetRoot().CreateInt("fake_int", -1, &insp);
    insp.GetRoot().CreateString("fake_str", "fake data", &insp);
    return fpromise::make_ok_promise(insp);
  };

  FakeInspectTree tree_with_detail(make_detail);
  tree_with_detail.UpdateSnapshot();
  // Ensure that the tree matches the expected node layout.
  ASSERT_NE(tree_with_detail.RootNode(), nullptr);
  ASSERT_THAT(*tree_with_detail.RootNode(), HasExpectedNodeLayout());

  // All nodes should exist this time.
  EXPECT_NE(tree_with_detail.InfoNode(), nullptr);
  EXPECT_NE(tree_with_detail.UsageNode(), nullptr);
  EXPECT_NE(tree_with_detail.VolumeNode(), nullptr);
  // The detail node should exist, and it's contents should match the callback above.
  ASSERT_NE(tree_with_detail.DetailNode(), nullptr);
  EXPECT_THAT(*tree_with_detail.DetailNode(),
              NodeMatches(AllOf(NameMatches(kDetailNodeName),
                                PropertyList(UnorderedElementsAre(
                                    IntIs("fake_int", -1), StringIs("fake_str", "fake data"))))));
}

// Validates the layout of the fs.info node and ensures that updates to properties are propagated.
TEST(VfsInspectTree, InfoNode) {
  FakeInspectTree inspect_tree{};

  // Test default-constructed values.
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.InfoNode(), nullptr);
  EXPECT_THAT(*inspect_tree.InfoNode(), NodePropertiesMatch(InfoData{}));

  // Set some other values and make sure the tree reflects them.
  InfoData info_data = {
      .id = 1,
      .type = 2,
      .name = "fakefs",
      .version_major = 3,
      .version_minor = 4,
      .oldest_minor_version = 5,
      .block_size = 1024,
      .max_filename_length = 255,
  };
  inspect_tree.SetInfoData(info_data);
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.InfoNode(), nullptr);
  EXPECT_THAT(*inspect_tree.InfoNode(), NodePropertiesMatch(info_data));

  info_data = {
      .name = "some other name",
      .max_filename_length = 64,
  };
  inspect_tree.SetInfoData(info_data);
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.InfoNode(), nullptr);
  EXPECT_THAT(*inspect_tree.InfoNode(), NodePropertiesMatch(info_data));
}

// Validates the layout of the fs.usage node and ensures that updates to properties are propagated.
TEST(VfsInspectTree, UsageNode) {
  FakeInspectTree inspect_tree{};

  // Test default-constructed values.
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.UsageNode(), nullptr);
  EXPECT_THAT(*inspect_tree.UsageNode(), NodePropertiesMatch(UsageData{}));

  // Set some other values and make sure the tree reflects them.
  UsageData usage_data = {
      .total_bytes = 512, .used_bytes = 256, .total_nodes = 128, .used_nodes = 64};
  inspect_tree.SetUsageData(usage_data);
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.UsageNode(), nullptr);
  EXPECT_THAT(*inspect_tree.UsageNode(), NodePropertiesMatch(usage_data));

  usage_data.used_bytes = 512;
  inspect_tree.SetUsageData(usage_data);
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.UsageNode(), nullptr);
  EXPECT_THAT(*inspect_tree.UsageNode(), NodePropertiesMatch(usage_data));
}

// Validates the layout of the fs.volume node and ensures that updates to properties are propagated.
TEST(VfsInspectTree, VolumeNode) {
  FakeInspectTree inspect_tree{};

  // Test default-constructed values.
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.VolumeNode(), nullptr);
  EXPECT_THAT(*inspect_tree.VolumeNode(), NodePropertiesMatch(VolumeData{}));

  // Set some other values and make sure the tree reflects them.
  VolumeData volume_data = {
      .size_info =
          {
              .size_bytes = 1024,
              .size_limit_bytes = 2048,
              .available_space_bytes = 0,
          },
      .out_of_space_events = 0,
  };
  inspect_tree.SetVolumeData(volume_data);
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.VolumeNode(), nullptr);
  EXPECT_THAT(*inspect_tree.VolumeNode(), NodePropertiesMatch(volume_data));

  volume_data.size_info.available_space_bytes = 1024;
  volume_data.out_of_space_events++;
  inspect_tree.SetVolumeData(volume_data);
  inspect_tree.UpdateSnapshot();
  ASSERT_NE(inspect_tree.VolumeNode(), nullptr);
  EXPECT_THAT(*inspect_tree.VolumeNode(), NodePropertiesMatch(volume_data));
}

}  // namespace fs_inspect
