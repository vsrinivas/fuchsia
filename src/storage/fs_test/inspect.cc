// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include <string>
#include <string_view>
#include <vector>

#include "src/lib/storage/vfs/cpp/inspect/inspect_tree.h"
#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using namespace ::testing;
using namespace inspect::testing;

using inspect::StringPropertyValue;
using inspect::UintPropertyValue;

// All properties we expect the fs.info node to contain.
constexpr std::string_view kAllInfoProperties[] = {
    fs_inspect::InfoData::kPropId,           fs_inspect::InfoData::kPropType,
    fs_inspect::InfoData::kPropName,         fs_inspect::InfoData::kPropVersionMajor,
    fs_inspect::InfoData::kPropVersionMinor, fs_inspect::InfoData::kPropOldestMinorVersion,
    fs_inspect::InfoData::kPropBlockSize,    fs_inspect::InfoData::kPropMaxFilenameLength,
};

// All properties we expect the fs.usage node to contain.
constexpr std::string_view kAllUsageProperties[] = {
    fs_inspect::UsageData::kPropTotalBytes,
    fs_inspect::UsageData::kPropUsedBytes,
    fs_inspect::UsageData::kPropTotalNodes,
    fs_inspect::UsageData::kPropUsedNodes,
};

// All properties we expect the fs.volume node to contain.
constexpr std::string_view kAllVolumeProperties[] = {
    fs_inspect::VolumeData::kPropSizeBytes,
    fs_inspect::VolumeData::kPropSizeLimitBytes,
    fs_inspect::VolumeData::kPropAvailableSpaceBytes,
    fs_inspect::VolumeData::kPropOutOfSpaceEvents,
};

// Create a vector of all property names found in the given node.
std::vector<std::string> GetPropertyNames(const inspect::NodeValue& node) {
  std::vector<std::string> properties;
  for (const auto& property : node.properties()) {
    properties.push_back(property.name());
  }
  return properties;
}

// Validates that the snapshot's hierarchy is compliant so that the test case invariants can be
// ensured. Use with ASSERT_NO_FATAL_FAILURE.
void ValidateHierarchy(const inspect::Hierarchy& root) {
  // Validate that the expected nodes in the hierarchy exist.
  ASSERT_THAT(root,
              ChildrenMatch(IsSupersetOf({NodeMatches(NameMatches(fs_inspect::kInfoNodeName)),
                                          NodeMatches(NameMatches(fs_inspect::kUsageNodeName)),
                                          NodeMatches(NameMatches(fs_inspect::kVolumeNodeName))})));

  // Ensure the expected properties under each node exist so that the invariants the getters above
  // rely on are valid (namely, that these specific nodes and their properties exist).

  // Validate fs.info node properties.
  const inspect::Hierarchy* info = root.GetByPath({fs_inspect::kInfoNodeName});
  ASSERT_NE(info, nullptr) << "Could not find node " << fs_inspect::kInfoNodeName;
  EXPECT_THAT(GetPropertyNames(info->node()), UnorderedElementsAreArray(kAllInfoProperties));

  // Validate fs.usage node properties.
  const inspect::Hierarchy* usage = root.GetByPath({fs_inspect::kUsageNodeName});
  ASSERT_NE(usage, nullptr) << "Could not find node " << fs_inspect::kUsageNodeName;
  EXPECT_THAT(GetPropertyNames(usage->node()), UnorderedElementsAreArray(kAllUsageProperties));

  // Validate fs.volume node properties.
  const inspect::Hierarchy* volume = root.GetByPath({fs_inspect::kVolumeNodeName});
  ASSERT_NE(volume, nullptr) << "Could not find node " << fs_inspect::kVolumeNodeName;
  EXPECT_THAT(GetPropertyNames(volume->node()), UnorderedElementsAreArray(kAllVolumeProperties));
}

// Parse the given fs.info node properties into a corresponding InfoData struct.
// Properties within the given node must both exist and be the correct type.
fs_inspect::InfoData GetInfoProperties(const inspect::NodeValue& info_node) {
  using fs_inspect::InfoData;
  return InfoData{
      .id = info_node.get_property<UintPropertyValue>(InfoData::kPropId)->value(),
      .type = info_node.get_property<UintPropertyValue>(InfoData::kPropType)->value(),
      .name = info_node.get_property<StringPropertyValue>(InfoData::kPropName)->value(),
      .version_major =
          info_node.get_property<UintPropertyValue>(InfoData::kPropVersionMajor)->value(),
      .version_minor =
          info_node.get_property<UintPropertyValue>(InfoData::kPropVersionMinor)->value(),
      .oldest_minor_version =
          info_node.get_property<UintPropertyValue>(InfoData::kPropOldestMinorVersion)->value(),
      .block_size = info_node.get_property<UintPropertyValue>(InfoData::kPropBlockSize)->value(),
      .max_filename_length =
          info_node.get_property<UintPropertyValue>(InfoData::kPropMaxFilenameLength)->value(),
  };
}

// Parse the given fs.usage node properties into a corresponding UsageData struct.
// Properties within the given node must both exist and be the correct type.
fs_inspect::UsageData GetUsageProperties(const inspect::NodeValue& usage_node) {
  using fs_inspect::UsageData;
  return UsageData{
      .total_bytes =
          usage_node.get_property<UintPropertyValue>(UsageData::kPropTotalBytes)->value(),
      .used_bytes = usage_node.get_property<UintPropertyValue>(UsageData::kPropUsedBytes)->value(),
      .total_nodes =
          usage_node.get_property<UintPropertyValue>(UsageData::kPropTotalNodes)->value(),
      .used_nodes = usage_node.get_property<UintPropertyValue>(UsageData::kPropUsedNodes)->value(),
  };
}

// Parse the given fs.volume node properties into a corresponding VolumeData struct.
// Properties within the given node must both exist and be the correct type.
fs_inspect::VolumeData GetVolumeProperties(const inspect::NodeValue& volume_node) {
  using fs_inspect::VolumeData;
  return VolumeData{
      .size_info =
          {
              .size_bytes =
                  volume_node.get_property<UintPropertyValue>(VolumeData::kPropSizeBytes)->value(),
              .size_limit_bytes =
                  volume_node.get_property<UintPropertyValue>(VolumeData::kPropSizeLimitBytes)
                      ->value(),
              .available_space_bytes =
                  volume_node.get_property<UintPropertyValue>(VolumeData::kPropAvailableSpaceBytes)
                      ->value(),
          },
      .out_of_space_events =
          volume_node.get_property<UintPropertyValue>(VolumeData::kPropOutOfSpaceEvents)->value(),
  };
}

// Obtain a snapshot from the underlying filesystem's inspect tree.
inspect::Hierarchy TakeSnapshot(zx_handle_t export_root) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("inspect-snapshot-thread");
  async::Executor executor(loop.dispatcher());

  fuchsia::inspect::TreePtr tree;
  async_dispatcher_t* dispatcher = executor.dispatcher();
  zx_status_t status = fdio_service_connect_at(export_root, "diagnostics/fuchsia.inspect.Tree",
                                               tree.NewRequest(dispatcher).TakeChannel().release());
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to connect to inspect service: %s",
                zx_status_get_string(status));

  std::condition_variable cv;
  std::mutex m;
  bool done = false;
  fpromise::result<inspect::Hierarchy> hierarchy_or_error;

  auto promise = inspect::ReadFromTree(std::move(tree))
                     .then([&](fpromise::result<inspect::Hierarchy>& result) {
                       {
                         std::unique_lock<std::mutex> lock(m);
                         hierarchy_or_error = std::move(result);
                         done = true;
                       }
                       cv.notify_all();
                     });

  executor.schedule_task(std::move(promise));

  std::unique_lock<std::mutex> lock(m);
  cv.wait(lock, [&done]() { return done; });

  loop.Quit();
  loop.JoinThreads();

  ZX_ASSERT_MSG(hierarchy_or_error.is_ok(), "Failed to obtain inspect tree snapshot!");
  return hierarchy_or_error.take_value();
}

class InspectTest : public FilesystemTest {
 protected:
  // Initializes the test case by taking an initial snapshot of the inspect tree, and validates
  // the overall node hierarchy/layout.
  void SetUp() override {
    // Take an initial snapshot.
    ASSERT_NO_FATAL_FAILURE(UpdateAndValidateSnapshot());
  }

  // Take a new snapshot of the filesystem's inspect tree and validate the layout for compliance.
  // Invalidates the previous hierarchy obtained by calling Root().
  //
  // All calls to this function *must* be wrapped with ASSERT_NO_FATAL_FAILURE. Failure to do so
  // can result in some test fixture methods segfaulting.
  void UpdateAndValidateSnapshot() {
    snapshot_ = TakeSnapshot(fs().GetOutgoingDirectory()->get());
    // Validate the inspect hierarchy. Ensures all nodes/properties exist and are the correct types.
    ASSERT_NO_FATAL_FAILURE(ValidateHierarchy(Root()));
  }

  // Reference to root hierarchy from last snapshot. After calling UpdateAndValidateSnapshot(),
  // existing references are invalidated and *must not* be used.
  const inspect::Hierarchy& Root() const {
    // All inspect properties are attached under a unique name based on the filesystem type.
    // This allows a unique path to query the properties which is important for lapis sampling.
    const inspect::Hierarchy* root = snapshot_.GetByPath({fs().GetTraits().name});
    ZX_ASSERT_MSG(
        root != nullptr,
        "Could not find named root node in filesystem hierarchy (expected node name = %s).",
        fs().GetTraits().name.c_str());
    return *root;
  }

  // Obtains InfoData containing values from the latest snapshot's fs.info node.
  // All calls to UpdateAndValidateSnapshot() must be wrapped by ASSERT_NO_FATAL_FAILURE,
  // otherwise this function can dereference a nullptr causing a segfault.
  fs_inspect::InfoData GetInfoData() const {
    return GetInfoProperties(Root().GetByPath({fs_inspect::kInfoNodeName})->node());
  }

  // Obtains UsageData containing values from the latest snapshot's fs.usage node.
  // All calls to UpdateAndValidateSnapshot() must be wrapped by ASSERT_NO_FATAL_FAILURE,
  // otherwise this function can dereference a nullptr causing a segfault.
  fs_inspect::UsageData GetUsageData() const {
    return GetUsageProperties(Root().GetByPath({fs_inspect::kUsageNodeName})->node());
  }

  // Obtains VolumeData containing values from the latest snapshot's fs.volume node.
  // All calls to UpdateAndValidateSnapshot() must be wrapped by ASSERT_NO_FATAL_FAILURE,
  // otherwise this function can dereference a nullptr causing a segfault.
  fs_inspect::VolumeData GetVolumeData() const {
    return GetVolumeProperties(Root().GetByPath({fs_inspect::kVolumeNodeName})->node());
  }

 private:
  // Last snapshot taken of the inspect tree.
  inspect::Hierarchy snapshot_ = {};
};

// Validate values in the fs.info node.
TEST_P(InspectTest, ValidateInfoNode) {
  fs_inspect::InfoData info_data = GetInfoData();
  // The filesystem name (type) should match those in the filesystem traits.
  ASSERT_EQ(info_data.name, fs().GetTraits().name);
  // The filesystem instance identifier should be a valid handle (i.e. non-zero).
  ASSERT_NE(info_data.id, ZX_HANDLE_INVALID);
  // The maximum filename length should be set (i.e. > 0).
  ASSERT_GT(info_data.max_filename_length, 0u);
}

// Validate values in the fs.usage node.
TEST_P(InspectTest, ValidateUsageNode) {
  fs_inspect::UsageData usage_data = GetUsageData();
  EXPECT_GT(usage_data.total_nodes, 0u);
  EXPECT_GT(usage_data.total_bytes, 0u);
  EXPECT_LE(usage_data.total_bytes,
            fs().options().device_block_count * fs().options().device_block_size);
  uint64_t orig_used_bytes = usage_data.used_bytes;
  uint64_t orig_used_nodes = usage_data.used_nodes;

  // Write a file to disk.
  std::string test_filename = GetPath("test_file");
  const size_t kDataWriteSize = 128ul * 1024ul;

  fbl::unique_fd fd(open(test_filename.c_str(), O_CREAT | O_RDWR, 0666));
  ASSERT_TRUE(fd);
  std::vector<uint8_t> data(kDataWriteSize);
  ASSERT_EQ(write(fd.get(), data.data(), data.size()), static_cast<ssize_t>(data.size()));
  ASSERT_EQ(fsync(fd.get()), 0);

  // Take a new inspect snapshot, ensure used_bytes/used_nodes are updated correctly.
  ASSERT_NO_FATAL_FAILURE(UpdateAndValidateSnapshot());
  usage_data = GetUsageData();
  // Used bytes should increase by at least the amount of written data, and we should now use
  // at least one more inode than before.
  EXPECT_GE(usage_data.used_bytes, orig_used_bytes + kDataWriteSize);
  EXPECT_GE(usage_data.used_nodes, orig_used_nodes + 1);
}

// Validate values in the fs.volume node.
TEST_P(InspectTest, ValidateVolumeNode) {
  fs_inspect::VolumeData volume_data = GetVolumeData();
  EXPECT_EQ(volume_data.out_of_space_events, 0u);
  if (fs().options().use_fvm) {
    uint64_t device_size = fs().options().device_block_count * fs().options().device_block_size;
    uint64_t init_fvm_size = fs().options().fvm_slice_size * fs().options().initial_fvm_slice_count;
    ASSERT_GT(device_size, 0u) << "Invalid block device size!";
    ASSERT_GT(init_fvm_size, 0u) << "Invalid FVM volume size!";

    // The reported volume size should be at least the amount of initial FVM slices, but not exceed
    // the size of the block device.
    EXPECT_GE(volume_data.size_info.size_bytes, init_fvm_size);
    EXPECT_LT(volume_data.size_info.size_bytes, device_size);

    // We should have some amount of free space, but not more than the size of the block device.
    EXPECT_GT(volume_data.size_info.available_space_bytes, 0u);
    EXPECT_LT(volume_data.size_info.available_space_bytes, device_size);

    // We do not set a volume size limit in fs_test currently, so this should always be zero.
    EXPECT_EQ(volume_data.size_info.size_limit_bytes, 0u);

  } else {
    // If we aren't using an FVM-backed filesystem, we should fail to query these properties from
    // the volume protocol, so they should all be set to zero.
    EXPECT_EQ(volume_data.size_info.available_space_bytes, 0u);
    EXPECT_EQ(volume_data.size_info.size_bytes, 0u);
    EXPECT_EQ(volume_data.size_info.size_limit_bytes, 0u);
  }
}

std::vector<TestFilesystemOptions> GetTestCombinations() {
  return MapAndFilterAllTestFilesystems(
      [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
        if (options.filesystem->GetTraits().supports_inspect) {
          return options;
        }
        return std::nullopt;
      });
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, InspectTest, ::testing::ValuesIn(GetTestCombinations()),
                         ::testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(InspectTest);

}  // namespace
}  // namespace fs_test
