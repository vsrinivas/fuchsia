// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests blobfs inspector behavior.

#include <blobfs/inspector/inspector.h>

#include <lib/disk-inspector/disk-inspector.h>

#include <fbl/macros.h>
#include <zxtest/zxtest.h>

#include "inspector/inspector-blobfs.h"
#include "inspector/root-object.h"
#include "inspector/superblock.h"

namespace blobfs {
namespace {

constexpr Superblock superblock = {};

// Mock Blobfs class
class MockInspectorBlobfs : public InspectorBlobfs {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(MockInspectorBlobfs);
  explicit MockInspectorBlobfs() {}

  const Superblock& GetSuperblock() const final { return superblock; }
};

TEST(InspectorTest, TestRoot) {
  auto fs = std::make_unique<MockInspectorBlobfs>();
  std::unique_ptr<RootObject> root_obj(new RootObject(std::move(fs)));
  ASSERT_STR_EQ(kRootName, root_obj->GetName());
  ASSERT_EQ(kRootNumElements, root_obj->GetNumElements());

  // Superblock.
  std::unique_ptr<disk_inspector::DiskObject> obj0 = root_obj->GetElementAt(0);
  ASSERT_STR_EQ(kSuperblockName, obj0->GetName());
  ASSERT_EQ(kSuperblockNumElements, obj0->GetNumElements());
}

TEST(InspectorTest, TestSuperblock) {
  Superblock sb;
  sb.magic0 = kBlobfsMagic0;
  sb.magic1 = kBlobfsMagic1;
  sb.version = kBlobfsVersion;
  sb.flags = kBlobFlagClean;
  sb.block_size = kBlobfsBlockSize;

  size_t size;
  const void* buffer = nullptr;

  std::unique_ptr<SuperblockObject> superblock(new SuperblockObject(sb));
  ASSERT_STR_EQ(kSuperblockName, superblock->GetName());
  ASSERT_EQ(kSuperblockNumElements, superblock->GetNumElements());

  std::unique_ptr<disk_inspector::DiskObject> obj0 = superblock->GetElementAt(0);
  obj0->GetValue(&buffer, &size);
  ASSERT_EQ(kBlobfsMagic0, *(reinterpret_cast<const uint64_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj1 = superblock->GetElementAt(1);
  obj1->GetValue(&buffer, &size);
  ASSERT_EQ(kBlobfsMagic1, *(reinterpret_cast<const uint64_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj2 = superblock->GetElementAt(2);
  obj2->GetValue(&buffer, &size);
  ASSERT_EQ(kBlobfsVersion, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj3 = superblock->GetElementAt(3);
  obj3->GetValue(&buffer, &size);
  ASSERT_EQ(kBlobFlagClean, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj4 = superblock->GetElementAt(4);
  obj4->GetValue(&buffer, &size);
  ASSERT_EQ(kBlobfsBlockSize, *(reinterpret_cast<const uint32_t*>(buffer)));
}

}  // namespace
}  // namespace blobfs
