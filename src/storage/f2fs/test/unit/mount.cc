// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
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
  for (uint32_t i = 0; i < kOptMaxNum; ++i) {
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
      case kOptInlineXattr:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountInlineXattr) == 0));
        break;
      case kOptInlineData:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountInlineData) == 0));
        break;
      case kOptInlineDentry:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountInlineDentry) == 0));
        break;
      case kOptForceLfs:
        ASSERT_EQ((value == 0), (superblock_info.TestOpt(kMountForceLfs) == 0));
        break;
    };
  }
  ASSERT_EQ(options.GetValue(kOptMaxNum, &value), ZX_ERR_INVALID_ARGS);
}

void MountTestDisableExt(F2fs *fs, uint32_t expectation) {
  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs, &root);
  Dir *root_dir = static_cast<Dir *>(root.get());
  bool result = (expectation > 0);

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

void TestSegmentType(F2fs *fs, Dir *root_dir, std::string_view name, bool is_dir,
                     std::vector<CursegType> &out) {
  fbl::RefPtr<fs::Vnode> vnode;
  uint32_t flag = (is_dir ? S_IFDIR : S_IFREG);
  nid_t nid = 100;
  uint32_t inode_ofs = 0;
  uint32_t indirect_node_ofs = 3;
  CursegType type;
  ASSERT_EQ(root_dir->Create(name, flag, &vnode), ZX_OK);
  VnodeF2fs *vn = static_cast<VnodeF2fs *>(vnode.get());

  // data block test
  {
    LockedPage page;
    vn->GrabCachePage(0, &page);
    type = fs->GetSegmentManager().GetSegmentType(*page, PageType::kData);
    out.push_back(type);
  }

  // Dnode block test
  {
    LockedPage page;
    fs->GetNodeVnode().GrabCachePage(vn->Ino(), &page);
    NodePage *node_page = &page.GetPage<NodePage>();
    node_page->FillNodeFooter(static_cast<nid_t>(node_page->GetIndex()), vn->Ino(), inode_ofs,
                              true);
    node_page->SetColdNode(*vn);
    type = fs->GetSegmentManager().GetSegmentType(*node_page, PageType::kNode);
    out.push_back(type);
  }

  // indirect node block test
  {
    LockedPage page;
    fs->GetNodeVnode().GrabCachePage(nid, &page);
    NodePage *node_page = &page.GetPage<NodePage>();
    node_page->FillNodeFooter(static_cast<nid_t>(node_page->GetIndex()), vn->Ino(),
                              indirect_node_ofs, true);
    node_page->SetColdNode(*vn);
    type = fs->GetSegmentManager().GetSegmentType(*node_page, PageType::kNode);
    out.push_back(type);
  }

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
  [[maybe_unused]] constexpr int cold_file = 2;

  [[maybe_unused]] constexpr int data_block = 0;
  constexpr int dnode_block = 1;
  constexpr int indirect_node_block = 2;

  for (int i = 0; i < 3; ++i) {
    results.clear();
    TestSegmentType(fs, root_dir, filenames[i], (i == dir_file), results);
    for (int j = 0; j < 3; ++j) {
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

TEST(MountTest, EnableDiscardOptions) {
  std::unique_ptr<f2fs::Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDiscard), 1), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  ASSERT_TRUE(fs->GetSuperblockInfo().TestOpt(kMountDiscard));

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(MountTest, InvalidOptions) {
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptActiveLogs), kMaxActiveLogs),
            ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptBgGcOff), 1), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptNoHeap), 1), ZX_ERR_INVALID_ARGS);
}

TEST(MountTest, MountException) {
  std::unique_ptr<Bcache> bc;
  uint64_t block_count = 20ull * 1024ull * 1024ull / kDefaultSectorSize;
  auto device =
      std::make_unique<block_client::FakeBlockDevice>(block_client::FakeBlockDevice::Config{
          .block_count = block_count, .block_size = kDefaultSectorSize, .supports_trim = true});
  bool readonly_device = false;
  auto bc_or = CreateBcache(std::move(device), &readonly_device);
  ASSERT_TRUE(bc_or.is_ok());

  MountOptions mount_options;
  ASSERT_EQ(Mount(mount_options, std::move(*bc_or), {}).status_value(), ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace f2fs
