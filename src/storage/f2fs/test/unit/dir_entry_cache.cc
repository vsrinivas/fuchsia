// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

using DirEntryCacheTest = F2fsFakeDevTestFixture;

TEST_F(DirEntryCacheTest, Basic) {
  std::unordered_set<std::string> child_set = {"alpha", "bravo", "charlie", "delta", "echo"};

  // Create children
  for (auto &child : child_set) {
    FileTester::CreateChild(root_dir_.get(), S_IFDIR, child.c_str());
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), child));
  }

  // Check if all children exist on the cache
  for (auto &child : child_set) {
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), child));
  }

  // Remove "bravo"
  FileTester::DeleteChild(root_dir_.get(), "bravo");
  child_set.erase("bravo");

  // Check if "bravo" is not exist on the cache
  ASSERT_FALSE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), "bravo"));

  // Check if all other children still exist on the cache
  for (auto &child : child_set) {
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), child));
  }

  // Lookup "alpha"
  {
    fbl::RefPtr<fs::Vnode> tmp;
    FileTester::Lookup(root_dir_.get(), "alpha", &tmp);
    ASSERT_EQ(tmp->Close(), ZX_OK);
  }

  // Check if "alpha" is at the head of the LRU list
  ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), "alpha"));

  // Lookup "charlie"
  {
    fbl::RefPtr<fs::Vnode> tmp;
    FileTester::Lookup(root_dir_.get(), "charlie", &tmp);
    ASSERT_EQ(tmp->Close(), ZX_OK);
  }

  // Check if "charlie" is at the head of the LRU list
  ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), "charlie"));
}

TEST_F(DirEntryCacheTest, SubDirectory) {
  // Create "alpha"
  FileTester::CreateChild(root_dir_.get(), S_IFDIR, "alpha");
  fbl::RefPtr<fs::Vnode> child_dir_vn;
  FileTester::Lookup(root_dir_.get(), "alpha", &child_dir_vn);
  ASSERT_TRUE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), "alpha"));

  // Create "alpha/bravo"
  fbl::RefPtr<Dir> child_dir = fbl::RefPtr<Dir>::Downcast(std::move(child_dir_vn));
  FileTester::CreateChild(child_dir.get(), S_IFDIR, "bravo");
  ASSERT_TRUE(fs_->GetDirEntryCache().IsElementInCache(child_dir->Ino(), "bravo"));

  // Delete "alpha/bravo"
  FileTester::DeleteChild(child_dir.get(), "bravo");
  ASSERT_FALSE(fs_->GetDirEntryCache().IsElementInCache(child_dir->Ino(), "bravo"));

  // Delete "alpha"
  ASSERT_EQ(child_dir->Close(), ZX_OK);
  FileTester::DeleteChild(root_dir_.get(), "alpha");
  ASSERT_FALSE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), "alpha"));

  // Create "alpha", and check if "alpha/bravo" is not exist
  FileTester::CreateChild(root_dir_.get(), S_IFDIR, "alpha");
  ASSERT_TRUE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), "alpha"));
  FileTester::Lookup(root_dir_.get(), "alpha", &child_dir_vn);
  child_dir = fbl::RefPtr<Dir>::Downcast(std::move(child_dir_vn));
  ASSERT_FALSE(fs_->GetDirEntryCache().IsElementInCache(child_dir->Ino(), "bravo"));

  // Create "alpha/bravo", move "alpha" to "charlie", and check if "charlie/bravo" is exist
  FileTester::CreateChild(child_dir.get(), S_IFDIR, "bravo");
  ASSERT_EQ(child_dir->Close(), ZX_OK);
  ASSERT_EQ(root_dir_->Rename(root_dir_, "alpha", "charlie", true, true), ZX_OK);
  FileTester::Lookup(root_dir_.get(), "charlie", &child_dir_vn);
  child_dir = fbl::RefPtr<Dir>::Downcast(std::move(child_dir_vn));
  ASSERT_TRUE(fs_->GetDirEntryCache().IsElementInCache(child_dir->Ino(), "bravo"));
  ASSERT_EQ(child_dir->Close(), ZX_OK);
}

TEST_F(DirEntryCacheTest, LRUEviction) {
  const uint32_t max_element =
      static_cast<uint32_t>(kDirEntryCacheSlabSize * kDirEntryCacheSlabCount) /
      sizeof(DirEntryCacheElement);

  std::unordered_set<std::string> child_set = {};

  // create children from 0 to |max_element| - 1
  for (uint32_t i = 0; i < max_element; ++i) {
    std::string child = std::to_string(i);
    FileTester::CreateChild(root_dir_.get(), S_IFDIR, child);
    child_set.insert(child);
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), child));
  }

  // Check if all children exist on the cache
  for (auto &child : child_set) {
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), child));
  }

  // Create one more child (|max_element|)
  {
    std::string child = std::to_string(max_element);
    FileTester::CreateChild(root_dir_.get(), S_IFDIR, child);
    child_set.insert(child);
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), child));
  }

  // Then, "0" should be removed from cache
  {
    std::string child = "0";
    ASSERT_FALSE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), child));
    child_set.erase(child);
  }

  // Cache hit for "1"
  {
    std::string child = "1";
    fbl::RefPtr<fs::Vnode> child_dir_vn;
    FileTester::Lookup(root_dir_.get(), child, &child_dir_vn);
    ASSERT_EQ(child_dir_vn->Close(), ZX_OK);
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), child));
  }

  // Create one more child (|max_element| + 1)
  {
    std::string child = std::to_string(max_element + 1);
    FileTester::CreateChild(root_dir_.get(), S_IFDIR, child);
    child_set.insert(child);
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), child));
  }

  // Then, "1" should exist on cache, and "2" should be removed
  {
    std::string child = "1";
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), child));

    child = "2";
    ASSERT_FALSE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), child));
  }

  // Lookup for "2". Then, "3" should be removed
  {
    std::string child = "2";
    fbl::RefPtr<fs::Vnode> child_dir_vn;
    FileTester::Lookup(root_dir_.get(), child, &child_dir_vn);
    ASSERT_EQ(child_dir_vn->Close(), ZX_OK);
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), child));

    child = "3";
    ASSERT_FALSE(fs_->GetDirEntryCache().IsElementInCache(root_dir_->Ino(), child));
  }
}

TEST_F(DirEntryCacheTest, CacheDataValidation) {
  const uint32_t nr_child = kNrDentryInBlock * 4;

  // Create children
  for (uint32_t i = 0; i < nr_child; ++i) {
    std::string child = std::to_string(i);
    FileTester::CreateChild(root_dir_.get(), S_IFDIR, child);
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), child));
  }

  // Access to some of children
  const uint32_t skip = 3;
  for (uint32_t i = 0; i < nr_child; i += skip) {
    std::string child = std::to_string(i);
    fbl::RefPtr<fs::Vnode> child_dir_vn;
    FileTester::Lookup(root_dir_.get(), child, &child_dir_vn);
    ASSERT_EQ(child_dir_vn->Close(), ZX_OK);
    ASSERT_TRUE(fs_->GetDirEntryCache().IsElementAtHead(root_dir_->Ino(), child));
  }

  // Check whether cached data is valid
  auto &map = fs_->GetDirEntryCache().GetMap();
  for (auto &cached : map) {
    ino_t parent_ino_from_key = cached.first.first;
    std::string child_name_from_key = cached.first.second;
    auto element = cached.second;

    // Validate cached parent ino
    ASSERT_EQ(element->GetParentIno(), parent_ino_from_key);
    ASSERT_EQ(element->GetParentIno(), root_dir_->Ino());
    // Validate cached child name
    ASSERT_EQ(element->GetName(), child_name_from_key);

    fbl::RefPtr<Page> page = nullptr;

    // To validate cached parent ino, read a page for cached index
    ASSERT_EQ(root_dir_->FindDataPage(element->GetDataPageIndex(), &page), ZX_OK);
    DentryBlock *dentry_block = page->GetAddress<DentryBlock>();

    uint32_t bit_pos = FindNextBit(dentry_block->dentry_bitmap, kNrDentryInBlock, 0);
    while (bit_pos < kNrDentryInBlock) {
      DirEntry *de = &dentry_block->dentry[bit_pos];
      uint32_t slots = (LeToCpu(de->name_len) + kNameLen - 1) / kNameLen;

      // If child name is found in the block, check if contents are valid
      if (std::string(reinterpret_cast<char *>(dentry_block->filename[bit_pos]),
                      element->GetName().length()) == element->GetName()) {
        // Validate cached dir entry
        // Make a copy for alignment
        DirEntry de_copied = *de;

        ASSERT_EQ(element->GetDirEntry().hash_code, de_copied.hash_code);
        ASSERT_EQ(element->GetDirEntry().ino, de_copied.ino);
        ASSERT_EQ(element->GetDirEntry().name_len, de_copied.name_len);
        ASSERT_EQ(element->GetDirEntry().file_type, de_copied.file_type);

        break;
      }

      bit_pos = FindNextBit(dentry_block->dentry_bitmap, kNrDentryInBlock, bit_pos + slots);
    }
    // If not found, |bit_pos| exceeds the bitmap length, |kNrDentryInBlock|
    ASSERT_LT(bit_pos, safemath::checked_cast<uint32_t>(kNrDentryInBlock));
  }
}

}  // namespace
}  // namespace f2fs
