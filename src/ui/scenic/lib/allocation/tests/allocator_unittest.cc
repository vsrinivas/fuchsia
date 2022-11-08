// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/allocation/allocator.h"

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "fuchsia/math/cpp/fidl.h"
#include "fuchsia/ui/composition/cpp/fidl.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/allocation/mock_buffer_collection_importer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using ::testing::_;
using ::testing::Return;

using allocation::BufferCollectionUsage;
using fuchsia::ui::composition::Allocator_RegisterBufferCollection_Result;
using fuchsia::ui::composition::BufferCollectionExportToken;
using fuchsia::ui::composition::BufferCollectionImportToken;
using fuchsia::ui::composition::RegisterBufferCollectionArgs;
using fuchsia::ui::composition::RegisterBufferCollectionUsage;
using fuchsia::ui::composition::RegisterBufferCollectionUsages;

namespace allocation {
namespace test {

#define REGISTER_BUFFER_COLLECTION(allocator, export_token, token, usage, expect_success)      \
  if (expect_success) {                                                                        \
    EXPECT_CALL(*mock_buffer_collection_importer_,                                             \
                ImportBufferCollection(fsl::GetKoid(export_token.value.get()), _, _, _, _))    \
        .WillOnce(testing::Invoke(                                                             \
            [](GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,                     \
               fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,                  \
               BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) { return true; })); \
  }                                                                                            \
  bool processed_callback = false;                                                             \
  allocator->RegisterBufferCollection(                                                         \
      CreateArgs(std::move(export_token), token, usage),                                       \
      [&processed_callback](Allocator_RegisterBufferCollection_Result result) {                \
        EXPECT_EQ(!expect_success, result.is_err());                                           \
        processed_callback = true;                                                             \
      });                                                                                      \
  EXPECT_TRUE(processed_callback);

RegisterBufferCollectionArgs CreateArgs(
    BufferCollectionExportToken export_token,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> buffer_collection_token,
    RegisterBufferCollectionUsage usage) {
  RegisterBufferCollectionArgs args;

  args.set_export_token(std::move(export_token));
  args.set_buffer_collection_token(std::move(buffer_collection_token));
  args.set_usage(usage);

  return args;
}

class AllocatorTest : public gtest::TestLoopFixture {
 public:
  AllocatorTest() {}

  void SetUp() override {
    sysmem_allocator_ = utils::CreateSysmemAllocatorSyncPtr("allocator_unittest::SetUp");

    mock_buffer_collection_importer_ = new MockBufferCollectionImporter();
    buffer_collection_importer_ =
        std::shared_ptr<BufferCollectionImporter>(mock_buffer_collection_importer_);

    // Capture uninteresting cleanup calls from Allocator dtor.
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(_, _))
        .Times(::testing::AtLeast(0));
  }

  void TearDown() override { RunLoopUntilIdle(); }

  std::shared_ptr<Allocator> CreateAllocator(RegisterBufferCollectionUsage usage) {
    std::vector<std::shared_ptr<BufferCollectionImporter>> default_importers;
    std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;

    bool use_default_importer = (usage == RegisterBufferCollectionUsage::DEFAULT) ? true : false;

    if (use_default_importer)
      default_importers.push_back(buffer_collection_importer_);
    else
      screenshot_importers.push_back(buffer_collection_importer_);

    return std::make_shared<Allocator>(
        context_provider_.context(), default_importers, screenshot_importers,
        utils::CreateSysmemAllocatorSyncPtr("allocator_unittest::CreateAllocator"));
  }

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> CreateToken() {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token;
    zx_status_t status = sysmem_allocator_->AllocateSharedCollection(token.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    status = token->Sync();
    EXPECT_EQ(status, ZX_OK);
    return token;
  }

 protected:
  MockBufferCollectionImporter* mock_buffer_collection_importer_;
  std::shared_ptr<BufferCollectionImporter> buffer_collection_importer_;
  sys::testing::ComponentContextProvider context_provider_;

 private:
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

class AllocatorTestParameterized
    : public AllocatorTest,
      public testing::WithParamInterface<RegisterBufferCollectionUsage> {};

INSTANTIATE_TEST_SUITE_P(, AllocatorTestParameterized,
                         testing::Values(RegisterBufferCollectionUsage::DEFAULT,
                                         RegisterBufferCollectionUsage::SCREENSHOT));

TEST_P(AllocatorTestParameterized, CreateAndDestroyAllocatorChannel) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);
  fuchsia::ui::composition::AllocatorPtr allocator_ptr;
  context_provider_.ConnectToPublicService(allocator_ptr.NewRequest());
  RunLoopUntilIdle();

  allocator_ptr.Unbind();
}

TEST_P(AllocatorTestParameterized, CreateAndDestroyMultipleAllocatorChannels) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);
  fuchsia::ui::composition::AllocatorPtr allocator_ptr1;
  context_provider_.ConnectToPublicService(allocator_ptr1.NewRequest());
  fuchsia::ui::composition::AllocatorPtr allocator_ptr2;
  context_provider_.ConnectToPublicService(allocator_ptr2.NewRequest());
  RunLoopUntilIdle();

  allocator_ptr1.Unbind();
  allocator_ptr2.Unbind();
}

TEST_P(AllocatorTestParameterized, RegisterBufferCollectionThroughAllocatorChannel) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);

  fuchsia::ui::composition::AllocatorPtr allocator_ptr;
  context_provider_.ConnectToPublicService(allocator_ptr.NewRequest());

  bool processed_callback = false;
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());
  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferCollection(koid, _, _, _, _))
      .WillOnce(Return(true));
  allocator_ptr->RegisterBufferCollection(
      CreateArgs(std::move(ref_pair.export_token), CreateToken(), usage),
      [&processed_callback](Allocator_RegisterBufferCollection_Result result) {
        EXPECT_FALSE(result.is_err());
        processed_callback = true;
      });
  RunLoopUntilIdle();
  EXPECT_TRUE(processed_callback);

  // Closing channel should not trigger ReleaseBufferCollection, because the client still holds a
  // BufferCollectionImportToken.
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid, _)).Times(0);
    allocator_ptr.Unbind();
  }
  // Destruction of Allocator instance triggers ReleaseBufferCollection.
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid, _)).Times(1);
    allocator.reset();
  }
}

TEST_P(AllocatorTestParameterized, RegisterBufferCollectionThroughMultipleAllocatorChannels) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);

  const int kNumAllocators = 3;
  std::vector<fuchsia::ui::composition::AllocatorPtr> allocator_ptrs;
  for (int i = 0; i < kNumAllocators; ++i) {
    fuchsia::ui::composition::AllocatorPtr allocator_ptr;
    context_provider_.ConnectToPublicService(allocator_ptr.NewRequest());
    allocator_ptrs.push_back(std::move(allocator_ptr));
  }

  for (auto& allocator_ptr : allocator_ptrs) {
    bool processed_callback = false;
    BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
    const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());
    EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferCollection(koid, _, _, _, _))
        .WillOnce(Return(true));
    allocator_ptr->RegisterBufferCollection(
        CreateArgs(std::move(ref_pair.export_token), CreateToken(), usage),
        [&processed_callback](Allocator_RegisterBufferCollection_Result result) {
          EXPECT_FALSE(result.is_err());
          processed_callback = true;
        });
    RunLoopUntilIdle();
    EXPECT_TRUE(processed_callback);
  }
}

// Tests that Allocator passes the Sysmem token to the importer. This is necessary since the client
// may block on buffers being allocated before presenting.
TEST_P(AllocatorTestParameterized, RegisterBufferCollectionValidCase) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), usage, true);
}

TEST_P(AllocatorTestParameterized, RegisterBufferCollectionErrorCases) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);

  // Sending an invalid export token is not valid.
  {
    BufferCollectionExportToken export_token;
    REGISTER_BUFFER_COLLECTION(allocator, export_token, CreateToken(), usage, false);
  }

  // Registering the same export token multiple times is not valid.
  {
    BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
    BufferCollectionExportToken export_token_dup;
    zx_status_t status =
        ref_pair.export_token.value.duplicate(ZX_RIGHT_SAME_RIGHTS, &export_token_dup.value);
    EXPECT_TRUE(status == ZX_OK);
    { REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), usage, true); }
    { REGISTER_BUFFER_COLLECTION(allocator, export_token_dup, CreateToken(), usage, false); }
  }

  // Passing an uninitiated buffer collection token is not valid.
  {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
    REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, std::move(token), usage, false);
  }

  // Passing a buffer collection token whose channel(s) have closed or gone out of scope is also
  // not valid.
  {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    {
      zx::channel local;
      zx::channel remote;
      zx::channel::create(0, &local, &remote);
      token = fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>(std::move(remote));
    }
    BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
    REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, std::move(token), usage, false);
  }

  // The buffer importer call can fail.
  {
    // Mock the importer call to fail.
    EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferCollection(_, _, _, _, _))
        .WillOnce(Return(false));
    BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
    REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), usage, false);
  }
}

// If we have multiple BufferCollectionImporters, some of them may properly import a buffer
// collection while others do not. We have to therefore make sure that if importer A properly
// imports a buffer collection and then importer B fails, that Flatland automatically releases the
// buffer collection from importer A.
TEST_P(AllocatorTestParameterized, BufferCollectionImportPassesAndFailsOnDifferentImportersTest) {
  const auto usage = GetParam();
  bool use_default_importer = (usage == RegisterBufferCollectionUsage::DEFAULT) ? true : false;

  // Create a second buffer collection importer.
  auto local_mock_buffer_collection_importer = new MockBufferCollectionImporter();
  auto local_buffer_collection_importer =
      std::shared_ptr<BufferCollectionImporter>(local_mock_buffer_collection_importer);

  // Create an allocator instance that has two BufferCollectionImporters.
  std::vector<std::shared_ptr<BufferCollectionImporter>> default_importers;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;

  if (use_default_importer) {
    default_importers.push_back(buffer_collection_importer_);
    default_importers.push_back(local_buffer_collection_importer);
  } else {
    screenshot_importers.push_back(buffer_collection_importer_);
    screenshot_importers.push_back(local_buffer_collection_importer);
  }

  std::shared_ptr<Allocator> allocator = std::make_shared<Allocator>(
      context_provider_.context(), default_importers, screenshot_importers,
      utils::CreateSysmemAllocatorSyncPtr(
          "allocator_unittest::BCImportPassesFailsOnDiffImporters"));

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());

  // Return failure from the local importer.
  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferCollection(koid, _, _, _, _))
      .Times(testing::AtLeast(0))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*local_mock_buffer_collection_importer, ImportBufferCollection(koid, _, _, _, _))
      .WillOnce(Return(false));

  // Expect buffer collection to be released from both instances.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid, _))
      .Times(testing::AtLeast(0));
  EXPECT_CALL(*local_mock_buffer_collection_importer, ReleaseBufferCollection(koid, _)).Times(0);

  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), usage, false);
}

TEST_P(AllocatorTestParameterized, DroppingImportTokensTriggerRelease) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), usage, true);

  // Drop the import token via reset().
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid, _)).Times(1);
    ref_pair.import_token.value.reset();
    RunLoopUntilIdle();
  }
}

TEST_P(AllocatorTestParameterized, RegisterAndReleaseMultipleBufferCollections) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);

  BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();
  const auto koid_1 = fsl::GetKoid(ref_pair_1.export_token.value.get());
  { REGISTER_BUFFER_COLLECTION(allocator, ref_pair_1.export_token, CreateToken(), usage, true); }

  BufferCollectionImportExportTokens ref_pair_2 = BufferCollectionImportExportTokens::New();
  const auto koid_2 = fsl::GetKoid(ref_pair_2.export_token.value.get());
  { REGISTER_BUFFER_COLLECTION(allocator, ref_pair_2.export_token, CreateToken(), usage, true); }

  // Drop the import token for the second buffer collection, which should be the only one released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid_2, _)).Times(1);
  ref_pair_2.import_token.value.reset();
  RunLoopUntilIdle();

  // Cleanup.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid_1, _)).Times(1);
  allocator.reset();
}

TEST_P(AllocatorTestParameterized, DuplicatedImportTokensKeepBufferCollectionRegistered) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());
  auto import_token_dup = ref_pair.DuplicateImportToken();

  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), usage, true);

  // Drop the import token via reset(). That should not trigger release because |import_token_dup|
  // is valid.
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid, _)).Times(0);
    ref_pair.import_token.value.reset();
    RunLoopUntilIdle();
  }

  // Drop the duped import token to trigger release.
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid, _)).Times(1);
    import_token_dup.value.reset();
    RunLoopUntilIdle();
  }
}

TEST_P(AllocatorTestParameterized, DestructorReleasesAllBufferCollections) {
  const auto usage = GetParam();
  std::shared_ptr<Allocator> allocator = CreateAllocator(usage);

  BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();
  { REGISTER_BUFFER_COLLECTION(allocator, ref_pair_1.export_token, CreateToken(), usage, true); }

  BufferCollectionImportExportTokens ref_pair_2 = BufferCollectionImportExportTokens::New();
  { REGISTER_BUFFER_COLLECTION(allocator, ref_pair_2.export_token, CreateToken(), usage, true); }

  // Cleanup.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(_, _)).Times(2);
  allocator.reset();
}

TEST_F(AllocatorTest, RegisterDefaultAndScreenshotBufferCollections) {
  // Create default importer.
  std::vector<std::shared_ptr<BufferCollectionImporter>> default_importers;
  auto default_mock_buffer_collection_importer = new MockBufferCollectionImporter();
  auto default_buffer_collection_importer =
      std::shared_ptr<BufferCollectionImporter>(default_mock_buffer_collection_importer);
  default_importers.push_back(default_buffer_collection_importer);

  // Create screenshot importer.
  std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;
  auto screenshot_mock_buffer_collection_importer = new MockBufferCollectionImporter();
  auto screenshot_buffer_collection_importer =
      std::shared_ptr<BufferCollectionImporter>(screenshot_mock_buffer_collection_importer);
  screenshot_importers.push_back(screenshot_buffer_collection_importer);

  // Create allocator.
  std::shared_ptr<Allocator> allocator = std::make_shared<Allocator>(
      context_provider_.context(), default_importers, screenshot_importers,
      utils::CreateSysmemAllocatorSyncPtr("allocator_unittest::RegisterDefaultAndScreenshotBCs"));

  // Register with the default importer.
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());

  EXPECT_CALL(*default_mock_buffer_collection_importer, ImportBufferCollection(koid, _, _, _, _))
      .WillOnce(Return(true));
  bool processed_callback = false;
  allocator->RegisterBufferCollection(
      CreateArgs(std::move(ref_pair.export_token), CreateToken(),
                 RegisterBufferCollectionUsage::DEFAULT),
      [&processed_callback](Allocator_RegisterBufferCollection_Result result) {
        EXPECT_FALSE(result.is_err());
        processed_callback = true;
      });
  EXPECT_TRUE(processed_callback);

  // Register with the screenshot screenshot.
  BufferCollectionImportExportTokens ref_pair2 = BufferCollectionImportExportTokens::New();
  const auto koid2 = fsl::GetKoid(ref_pair2.export_token.value.get());

  EXPECT_CALL(*screenshot_mock_buffer_collection_importer,
              ImportBufferCollection(koid2, _, _, _, _))
      .WillOnce(Return(true));
  processed_callback = false;
  allocator->RegisterBufferCollection(
      CreateArgs(std::move(ref_pair2.export_token), CreateToken(),
                 RegisterBufferCollectionUsage::SCREENSHOT),
      [&processed_callback](Allocator_RegisterBufferCollection_Result result) {
        EXPECT_FALSE(result.is_err());
        processed_callback = true;
      });
  EXPECT_TRUE(processed_callback);
}

TEST_F(AllocatorTest, RegisterBufferCollectionCombined) {
  // Create default importer.
  std::vector<std::shared_ptr<BufferCollectionImporter>> default_importers;
  auto default_mock_buffer_collection_importer = new MockBufferCollectionImporter();
  auto default_buffer_collection_importer =
      std::shared_ptr<BufferCollectionImporter>(default_mock_buffer_collection_importer);
  default_importers.push_back(default_buffer_collection_importer);

  // Create screenshot importer.
  std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;
  auto screenshot_mock_buffer_collection_importer = new MockBufferCollectionImporter();
  auto screenshot_buffer_collection_importer =
      std::shared_ptr<BufferCollectionImporter>(screenshot_mock_buffer_collection_importer);
  screenshot_importers.push_back(screenshot_buffer_collection_importer);

  // Create allocator.
  std::shared_ptr<Allocator> allocator = std::make_shared<Allocator>(
      context_provider_.context(), default_importers, screenshot_importers,
      utils::CreateSysmemAllocatorSyncPtr("allocator_unittest::RegisterBufferCollectionCombined"));

  // Register with the default importer and the screenshot importer.
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());

  EXPECT_CALL(*default_mock_buffer_collection_importer, ImportBufferCollection(koid, _, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*screenshot_mock_buffer_collection_importer, ImportBufferCollection(koid, _, _, _, _))
      .WillOnce(Return(true));

  RegisterBufferCollectionUsages usages;
  usages |= RegisterBufferCollectionUsages::DEFAULT;
  usages |= RegisterBufferCollectionUsages::SCREENSHOT;

  RegisterBufferCollectionArgs args;
  args.set_export_token(std::move(ref_pair.export_token));
  args.set_buffer_collection_token(CreateToken());
  args.set_usages(usages);

  bool processed_callback = false;
  allocator->RegisterBufferCollection(
      std::move(args), [&processed_callback](Allocator_RegisterBufferCollection_Result result) {
        EXPECT_FALSE(result.is_err());
        processed_callback = true;
      });
  EXPECT_TRUE(processed_callback);

  // Cleanup.
  EXPECT_CALL(*default_mock_buffer_collection_importer,
              ReleaseBufferCollection(_, BufferCollectionUsage::kClientImage))
      .Times(1);
  EXPECT_CALL(*screenshot_mock_buffer_collection_importer,
              ReleaseBufferCollection(_, BufferCollectionUsage::kRenderTarget))
      .Times(1);

  allocator.reset();
}

}  // namespace test
}  // namespace allocation
