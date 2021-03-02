// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/allocator.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/flatland/buffers/buffer_collection_ref_pair.h"
#include "src/ui/scenic/lib/flatland/buffers/mock_buffer_collection_importer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using ::testing::_;
using ::testing::Return;

using flatland::Allocator;
using flatland::BufferCollectionImporter;
using flatland::BufferCollectionRefPair;
using flatland::MockBufferCollectionImporter;
using fuchsia::ui::scenic::internal::Allocator_RegisterBufferCollection_Result;
using fuchsia::ui::scenic::internal::BufferCollectionExportToken;
using fuchsia::ui::scenic::internal::BufferCollectionImportToken;
using sysmem_util::GlobalBufferCollectionId;

namespace {

#define REGISTER_BUFFER_COLLECTION(allocator, export_token, token, expect_success)                \
  if (expect_success) {                                                                           \
    EXPECT_CALL(*mock_buffer_collection_importer_,                                                \
                ImportBufferCollection(fsl::GetKoid(export_token.value.get()), _, _))             \
        .WillOnce(testing::Invoke(                                                                \
            [](sysmem_util::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,           \
               fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>) { return true; })); \
  }                                                                                               \
  bool processed_callback = false;                                                                \
  allocator->RegisterBufferCollection(                                                            \
      std::move(export_token), token,                                                             \
      [&processed_callback](Allocator_RegisterBufferCollection_Result result) {                   \
        EXPECT_EQ(!expect_success, result.is_err());                                              \
        processed_callback = true;                                                                \
      });                                                                                         \
  EXPECT_TRUE(processed_callback);

class AllocatorTest : public gtest::TestLoopFixture {
 public:
  AllocatorTest() {}

  void SetUp() override {
    sysmem_allocator_ = utils::CreateSysmemAllocatorSyncPtr();

    mock_buffer_collection_importer_ = new MockBufferCollectionImporter();
    buffer_collection_importer_ =
        std::shared_ptr<BufferCollectionImporter>(mock_buffer_collection_importer_);

    // Capture uninteresting cleanup calls from Allocator dtor.
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(_))
        .Times(::testing::AtLeast(0));
  }

  void TearDown() override { RunLoopUntilIdle(); }

  std::shared_ptr<Allocator> CreateAllocator() {
    std::vector<std::shared_ptr<BufferCollectionImporter>> importers;
    importers.push_back(buffer_collection_importer_);
    return std::make_shared<Allocator>(importers,
                                       utils::CreateSysmemAllocatorSyncPtr("-allocator"));
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

 private:
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace

namespace flatland {
namespace test {

// Tests that Allocator passes the Sysmem token to the importer. This is necessary since the client
// may block on buffers being allocated before presenting.
TEST_F(AllocatorTest, RegisterBufferCollectionValidCase) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  BufferCollectionRefPair ref_pair = BufferCollectionRefPair::New();
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);
}

TEST_F(AllocatorTest, RegisterBufferCollectionErrorCases) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  // Sending an invalid export token is not valid.
  {
    BufferCollectionExportToken export_token;
    REGISTER_BUFFER_COLLECTION(allocator, export_token, CreateToken(), false);
  }

  // Registering the same export token multiple times is not valid.
  {
    BufferCollectionRefPair ref_pair = BufferCollectionRefPair::New();
    BufferCollectionExportToken export_token_dup;
    zx_status_t status =
        ref_pair.export_token.value.duplicate(ZX_RIGHT_SAME_RIGHTS, &export_token_dup.value);
    EXPECT_TRUE(status == ZX_OK);
    { REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true); }
    { REGISTER_BUFFER_COLLECTION(allocator, export_token_dup, CreateToken(), false); }
  }

  // Passing an uninitiated buffer collection token is not valid.
  {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    BufferCollectionRefPair ref_pair = BufferCollectionRefPair::New();
    REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, std::move(token), false);
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
    BufferCollectionRefPair ref_pair = BufferCollectionRefPair::New();
    REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, std::move(token), false);
  }

  // The buffer importer call can fail.
  {
    // Mock the importer call to fail.
    EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferCollection(_, _, _))
        .WillOnce(Return(false));
    BufferCollectionRefPair ref_pair = BufferCollectionRefPair::New();
    REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), false);
  }
}

// If we have multiple BufferCollectionImporters, some of them may properly import a buffer
// collection while others do not. We have to therefore make sure that if importer A properly
// imports a buffer collection and then importer B fails, that Flatland automatically releases the
// buffer collection from importer A.
TEST_F(AllocatorTest, BufferCollectionImportPassesAndFailsOnDifferentimportersTest) {
  // Create a second buffer collection importer.
  auto local_mock_buffer_collection_importer = new MockBufferCollectionImporter();
  auto local_buffer_collection_importer =
      std::shared_ptr<BufferCollectionImporter>(local_mock_buffer_collection_importer);

  // Create an allocator instance that has two BufferCollectionImporters.
  std::vector<std::shared_ptr<BufferCollectionImporter>> importers(
      {buffer_collection_importer_, local_buffer_collection_importer});
  std::shared_ptr<Allocator> allocator =
      std::make_shared<Allocator>(importers, utils::CreateSysmemAllocatorSyncPtr());

  BufferCollectionRefPair ref_pair = BufferCollectionRefPair::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());

  // Return failure from the local importer.
  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferCollection(koid, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*local_mock_buffer_collection_importer, ImportBufferCollection(koid, _, _))
      .WillOnce(Return(false));

  // Expect buffer collection to be released from both instances.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid)).Times(1);
  EXPECT_CALL(*local_mock_buffer_collection_importer, ReleaseBufferCollection(koid)).Times(0);

  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), false);
}

TEST_F(AllocatorTest, DroppingImportTokensTriggerRelease) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  BufferCollectionRefPair ref_pair = BufferCollectionRefPair::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);

  // Drop the import token via reset().
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid)).Times(1);
    ref_pair.import_token.value.reset();
    RunLoopUntilIdle();
  }
}

TEST_F(AllocatorTest, RegisterAndReleaseMultipleBufferCollections) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  BufferCollectionRefPair ref_pair_1 = BufferCollectionRefPair::New();
  const auto koid_1 = fsl::GetKoid(ref_pair_1.export_token.value.get());
  { REGISTER_BUFFER_COLLECTION(allocator, ref_pair_1.export_token, CreateToken(), true); }

  BufferCollectionRefPair ref_pair_2 = BufferCollectionRefPair::New();
  const auto koid_2 = fsl::GetKoid(ref_pair_2.export_token.value.get());
  { REGISTER_BUFFER_COLLECTION(allocator, ref_pair_2.export_token, CreateToken(), true); }

  // Drop the import token for the second buffer collection, which should be the only one released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid_2)).Times(1);
  ref_pair_2.import_token.value.reset();
  RunLoopUntilIdle();

  // Cleanup.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid_1)).Times(1);
  allocator.reset();
}

TEST_F(AllocatorTest, DuplicatedImportTokensKeepBufferCollectionRegistered) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  BufferCollectionRefPair ref_pair = BufferCollectionRefPair::New();
  const auto koid = fsl::GetKoid(ref_pair.export_token.value.get());
  auto import_token_dup = ref_pair.DuplicateImportToken();

  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);

  // Drop the import token via reset(). That should not trigger release because |import_token_dup|
  // is valid.
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid)).Times(0);
    ref_pair.import_token.value.reset();
    RunLoopUntilIdle();
  }

  // Drop the duped import token to trigger release.
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(koid)).Times(1);
    import_token_dup.value.reset();
    RunLoopUntilIdle();
  }
}

TEST_F(AllocatorTest, DestructorReleasesAllBufferCollections) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  BufferCollectionRefPair ref_pair_1 = BufferCollectionRefPair::New();
  const auto koid_1 = fsl::GetKoid(ref_pair_1.export_token.value.get());
  { REGISTER_BUFFER_COLLECTION(allocator, ref_pair_1.export_token, CreateToken(), true); }

  BufferCollectionRefPair ref_pair_2 = BufferCollectionRefPair::New();
  const auto koid_2 = fsl::GetKoid(ref_pair_2.export_token.value.get());
  { REGISTER_BUFFER_COLLECTION(allocator, ref_pair_2.export_token, CreateToken(), true); }

  // Cleanup.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(_)).Times(2);
  allocator.reset();
}

}  // namespace test
}  // namespace flatland
