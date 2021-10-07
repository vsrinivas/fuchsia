// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

using block_client::FakeBlockDevice;
constexpr uint32_t kMountVerifyTest = 0;
constexpr uint32_t kMountDisableExtTest = 1;
constexpr uint32_t kMountActiveLogsTest = 2;

void MountTestVerifyOptions(F2fs *fs, MountOptions &options) {
  uint32_t value;
  SuperblockInfo &superblock_info = fs->GetSuperblockInfo();
  for (uint32_t i = 0; i < kOptMaxNum; i++) {
    ASSERT_EQ(options.GetValue(i, &value), ZX_OK);
    switch (i) {
      case kOptActiveLogs:
        ASSERT_EQ(static_cast<uint32_t>(superblock_info.GetActiveLogs()), value);
        break;
      case kOptDiscard:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountDiscard) == 0));
        break;
      case kOptBgGcOff:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountBgGcOff) == 0));
        break;
      case kOptNoHeap:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountNoheap) == 0));
        break;
      case kOptDisableExtIdentify:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountDisableExtIdentify) == 0));
        break;
      case kOptNoUserXAttr:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountNoXAttr) == 0));
        break;
      case kOptNoAcl:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountNoAcl) == 0));
        break;
      case kOptDisableRollForward:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountDisableRollForward) == 0));
        break;
    };
  }
  ASSERT_EQ(options.GetValue(kOptMaxNum, &value), ZX_ERR_INVALID_ARGS);
}

void MountTestDisableExt(F2fs *fs, uint32_t expectation) {
  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs, &root);
  Dir *root_dir = static_cast<Dir *>(root.get());
  bool result = (expectation > 0 ? true : false);

  for (const char *ext_item : kMediaExtList) {
    std::string name = "test.";
    name += ext_item;
    fbl::RefPtr<fs::Vnode> vnode;
    // create regular files with cold file extensions
    ASSERT_EQ(root_dir->Create(name, S_IFREG, &vnode), ZX_OK);
    File *file = static_cast<File *>(vnode.get());
    ASSERT_EQ(NodeManager::IsColdFile(*file), result);
    vnode->Close();
  }

  ASSERT_EQ(root->Close(), ZX_OK);
  root = nullptr;
}

void TestSegmentType(F2fs *fs, Dir *root_dir, const std::string_view name, bool is_dir,
                     std::vector<CursegType> &out) {
  fbl::RefPtr<fs::Vnode> vnode;
  uint32_t flag = (is_dir ? S_IFDIR : S_IFREG);
  nid_t nid = 100;
  uint32_t inode_ofs = 0;
  uint32_t indirect_node_ofs = 3;
  ASSERT_EQ(root_dir->Create(name, flag, &vnode), ZX_OK);
  VnodeF2fs *vn = static_cast<VnodeF2fs *>(vnode.get());

  // data block test
  Page *page = GrabCachePage(vn, vn->Ino(), 0);
  CursegType type = fs->GetSegmentManager().GetSegmentType(page, PageType::kData);
  out.push_back(type);
  F2fsPutPage(page, 1);

  // Dnode block test
  page = GrabCachePage(nullptr, fs->GetSuperblockInfo().GetNodeIno(), vn->Ino());
  NodeManager::FillNodeFooter(*page, static_cast<nid_t>(page->index), vn->Ino(), inode_ofs, true);
  NodeManager::SetColdNode(*vn, *page);
  type = fs->GetSegmentManager().GetSegmentType(page, PageType::kNode);
  out.push_back(type);
  F2fsPutPage(page, 1);

  // indirect node block test
  page = GrabCachePage(nullptr, fs->GetSuperblockInfo().GetNodeIno(), nid);
  NodeManager::FillNodeFooter(*page, static_cast<nid_t>(page->index), vn->Ino(), indirect_node_ofs,
                              true);
  NodeManager::SetColdNode(*vn, *page);
  type = fs->GetSegmentManager().GetSegmentType(page, PageType::kNode);
  out.push_back(type);
  F2fsPutPage(page, 1);
  vnode->Close();
}

void MountTestActiveLogs(F2fs *fs, MountOptions options) {
  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs, &root);
  Dir *root_dir = static_cast<Dir *>(root.get());
  const char *filenames[] = {"dir", "warm.exe", "cold.mp4"};
  std::vector<CursegType> results(3, CursegType::kNoCheckType);
  uint32_t num_logs = 0;
  ASSERT_EQ(options.GetValue(kOptActiveLogs, &num_logs), ZX_OK);

  constexpr int dir_file = 0;
  constexpr int warm_file = 1;
  __UNUSED constexpr int cold_file = 2;

  __UNUSED constexpr int data_block = 0;
  constexpr int dnode_block = 1;
  constexpr int indirect_node_block = 2;

  for (int i = 0; i < 3; i++) {
    results.clear();
    TestSegmentType(fs, root_dir, filenames[i], (i == dir_file), results);
    for (int j = 0; j < 3; j++) {
      CursegType type = results[j];
      if (j == indirect_node_block) {
        if (num_logs > 2)
          ASSERT_EQ(type, CursegType::kCursegColdNode);
        else
          ASSERT_EQ(type, CursegType::kCursegHotNode);
      } else if (j == dnode_block) {
        if (i == dir_file || num_logs == 2) {
          ASSERT_EQ(type, CursegType::kCursegHotNode);
        } else if (num_logs == 6) {
          ASSERT_EQ(type, CursegType::kCursegWarmNode);
        } else
          ASSERT_EQ(type, CursegType::kCursegColdNode);

      } else {  // data block case
        if (i == dir_file || num_logs == 2) {
          ASSERT_EQ(type, CursegType::kCursegHotData);
        } else {
          if (i == warm_file && num_logs == 6)
            ASSERT_EQ(type, CursegType::kCursegWarmData);
          else
            ASSERT_EQ(type, CursegType::kCursegColdData);
        }
      }
    }
  }

  ASSERT_EQ(root->Close(), ZX_OK);
  root = nullptr;
}

void MountTestMain(MountOptions &options, uint32_t test, uint32_t priv) {
  std::unique_ptr<f2fs::Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  switch (test) {
    case kMountVerifyTest:
      MountTestVerifyOptions(fs.get(), options);
      break;
    case kMountDisableExtTest:
      MountTestDisableExt(fs.get(), priv);
      break;
    case kMountActiveLogsTest:
      MountTestActiveLogs(fs.get(), options);
      break;
    default:
      ASSERT_EQ(0, 1);
      break;
  };

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(MountTest, Verify) {
  MountOptions options;
  MountTestMain(options, kMountVerifyTest, 0);
}

TEST(MountTest, DisableExtOptions) {
  constexpr uint32_t ShouldNotBeCold = 0;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableExtIdentify), 1), ZX_OK);
  MountTestMain(options, kMountDisableExtTest, ShouldNotBeCold);
}

TEST(MountTest, EnableExtOptions) {
  constexpr uint32_t ShouldBeCold = 1;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableExtIdentify), 0), ZX_OK);
  MountTestMain(options, kMountDisableExtTest, ShouldBeCold);
}

TEST(MountTest, ActiveLogsOptions) {
  for (uint32_t i = 2; i <= 6; i += 2) {
    MountOptions options{};
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptActiveLogs), i), ZX_OK);
    MountTestMain(options, kMountActiveLogsTest, 0);
  }
}

}  // namespace
}  // namespace f2fs
