// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

TEST(DirTest, DentryReuse) {
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> test_dir;
  ASSERT_EQ(root_dir->Create("test", S_IFDIR, &test_dir), ZX_OK);

  fbl::RefPtr<VnodeF2fs> test_dir_vn = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_dir));

  Dir *test_dir_ptr = static_cast<Dir *>(test_dir_vn.get());

  std::unordered_set<std::string> child_set = {"a", "b", "c", "d", "e"};

  for (auto iter : child_set) {
    unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, iter);
  }
  ASSERT_EQ(test_dir_vn->GetSize(), kPageCacheSize);

  // remove "b" and "d"
  unittest_lib::DeleteChild(test_dir_ptr, "b");
  child_set.erase("b");
  unittest_lib::DeleteChild(test_dir_ptr, "d");
  child_set.erase("d");

  // Check remain children are in first dentry page
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 0, child_set);

  // create "f" and "g", and rename "a" to "h"
  unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, "f");
  child_set.insert("f");
  unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, "g");
  child_set.insert("g");

  ASSERT_EQ(test_dir_ptr->Rename(test_dir_vn, "a", "h", true, true), ZX_OK);
  child_set.erase("a");
  child_set.insert("h");

  // Check children are in first dentry page
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 0, child_set);

  // fill all dentry slots in first dentry page
  unsigned int child_count = child_set.size();
  for (; child_count < kNrDentryInBlock - 2; ++child_count) {
    unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, std::to_string(child_count));
    child_set.insert(std::to_string(child_count));
  }

  // Dir size should not be increased yet
  ASSERT_EQ(test_dir_vn->GetSize(), kPageCacheSize);

  // Check children are in first dentry page
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 0, child_set);

  // if one more child created, new dentry page will be allocated
  std::unordered_set<std::string> child_set_second_page;
  unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, std::to_string(child_count));
  child_set_second_page.insert(std::to_string(child_count));

  ASSERT_EQ(test_dir_vn->GetSize(), kPageCacheSize * 2);

  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 1, child_set_second_page);

  // Delete the last child, then the second page should not be accessed
  unittest_lib::DeleteChild(test_dir_ptr, std::to_string(child_count));
  std::unordered_set<std::string> empty_set;
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 1, empty_set);

  // Delete all children, then check empty dir
  for (auto iter : child_set) {
    unittest_lib::DeleteChild(test_dir_ptr, iter);
  }
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 0, empty_set);

  ASSERT_EQ(test_dir_vn->Close(), ZX_OK);
  test_dir_vn = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  unittest_lib::Unmount(std::move(fs), &bc);
}

TEST(DirTest, DentryBucket) {
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> test_dir;
  ASSERT_EQ(root_dir->Create("test", S_IFDIR, &test_dir), ZX_OK);

  fbl::RefPtr<VnodeF2fs> test_dir_vn = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_dir));

  Dir *test_dir_ptr = static_cast<Dir *>(test_dir_vn.get());

  // fill level-0 dentry blocks, since it has only one bucket
  std::unordered_set<std::string> child_set;
  unsigned int child_count = 0;
  for (; child_count < kNrDentryInBlock * 2 - 2; ++child_count) {
    unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, std::to_string(child_count));
    child_set.insert(std::to_string(child_count));
  }

  // size should be as same as 2 pages
  ASSERT_EQ(test_dir_vn->GetSize(), kPageCacheSize * 2);

  // at level 1, child will be devided into two buckets, depending on their hash value
  std::unordered_set<std::string> first_bucket_child;
  std::unordered_set<std::string> second_bucket_child;
  for (; child_count < kNrDentryInBlock * 3 - 2; ++child_count) {
    std::string name(std::to_string(child_count));
    unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, name);

    unsigned int bucket_id = DentryHash(name.data(), name.length()) % 2;

    if (bucket_id == 0) {
      first_bucket_child.insert(name);
    } else {
      second_bucket_child.insert(name);
    }
  }

  // check level 1, bucket 0
  unsigned int bidx = Dir::DirBlockIndex(1, 0);
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, bidx, first_bucket_child);

  // delete all children in level 1, bucket 0
  for (auto iter : first_bucket_child) {
    unittest_lib::DeleteChild(test_dir_ptr, iter);
  }
  std::unordered_set<std::string> empty_set;
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, bidx, empty_set);

  // check level 1, bucket 1
  bidx = Dir::DirBlockIndex(1, 1);
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, bidx, second_bucket_child);

  // delete all children in level 1, bucket 1
  for (auto iter : second_bucket_child) {
    unittest_lib::DeleteChild(test_dir_ptr, iter);
  }
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, bidx, empty_set);

  // Delete all children in level 0, then check empty dir
  for (auto iter : child_set) {
    unittest_lib::DeleteChild(test_dir_ptr, iter);
  }
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 0, empty_set);

  ASSERT_EQ(test_dir_vn->Close(), ZX_OK);
  test_dir_vn = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  unittest_lib::Unmount(std::move(fs), &bc);
}

TEST(DirTest, MultiSlotDentry) {
  auto seed = time(nullptr);
  srand(seed);
  std::cout << "Random seed for DirTest.MultiSlotDentry: " << seed << std::endl;

  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> test_dir;
  ASSERT_EQ(root_dir->Create("test", S_IFDIR, &test_dir), ZX_OK);

  fbl::RefPtr<VnodeF2fs> test_dir_vn = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_dir));

  Dir *test_dir_ptr = static_cast<Dir *>(test_dir_vn.get());

  // fill first dentry page
  unsigned int slots_filled = 2;
  unsigned int max_slots = (kMaxNameLen + kNameLen - 1) / kNameLen;
  std::unordered_set<std::string> child_set;
  while (slots_filled <= kNrDentryInBlock - max_slots) {
    unsigned int namelen = rand() % kMaxNameLen + 1;
    std::string name = unittest_lib::GetRandomName(namelen);

    unsigned int slots = (namelen + kNameLen - 1) / kNameLen;

    if (child_set.find(name) != child_set.end())
      continue;

    unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, name);
    child_set.insert(name);

    slots_filled += slots;
  }

  // check only one dentry page
  ASSERT_EQ(test_dir_vn->GetSize(), kPageCacheSize);

  // Check children are in first dentry page
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 0, child_set);

  // New child with large name than slot, then new dentry page allocated
  std::unordered_set<std::string> child_second_page;
  unsigned int namelen = (kNrDentryInBlock - slots_filled) * kNameLen + 1;
  std::string name;
  do {
    name = unittest_lib::GetRandomName(namelen);
  } while (child_set.find(name) != child_set.end());

  unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, name);
  child_second_page.insert(name);

  ASSERT_EQ(test_dir_vn->GetSize(), kPageCacheSize * 2);

  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 1, child_second_page);

  // Create new child that can be written in renaming slots in the first page,
  // then dentry will be written in the first page.
  namelen = (kNrDentryInBlock - slots_filled) * kNameLen;
  do {
    name = unittest_lib::GetRandomName(namelen);
  } while (child_set.find(name) != child_set.end() &&
           child_second_page.find(name) != child_second_page.end());

  unittest_lib::CreateChild(test_dir_ptr, S_IFDIR, name);
  child_set.insert(name);

  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 0, child_set);

  // Delete all, and check empty dir
  child_set.merge(child_second_page);
  for (auto iter : child_set) {
    unittest_lib::DeleteChild(test_dir_ptr, iter);
  }

  std::unordered_set<std::string> empty_set;
  unittest_lib::CheckChildrenInBlock(test_dir_ptr, 0, empty_set);

  ASSERT_EQ(test_dir_vn->Close(), ZX_OK);
  test_dir_vn = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  unittest_lib::Unmount(std::move(fs), &bc);
}

}  // namespace
}  // namespace f2fs
