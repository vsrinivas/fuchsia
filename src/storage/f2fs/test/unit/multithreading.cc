// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

TEST(MultiThreads, Truncate) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Disable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> test_file;
  root_dir->Create("test2", S_IFREG, &test_file);
  fbl::RefPtr<f2fs::File> vn = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file));

  constexpr int kNTry = 1000;
  uint8_t buf[kPageSize * 2] = {1};
  FileTester::AppendToFile(vn.get(), buf, sizeof(buf));
  std::thread thread1 = std::thread([&]() {
    for (int i = 0; i < kNTry; ++i) {
      size_t out_actual;
      ASSERT_EQ(vn->Write(buf, sizeof(buf), 0, &out_actual), ZX_OK);
    }
  });
  std::thread thread2 = std::thread([&]() {
    for (int i = 0; i < kNTry; ++i) {
      ASSERT_EQ(vn->Truncate(0), ZX_OK);
    }
  });
  thread1.join();
  thread2.join();
  vn->Close();
  vn = nullptr;
  root_dir->Close();
  root_dir = nullptr;
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(MultiThreads, Write) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc, 2097152);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> test_file1, test_file2;
  root_dir->Create("test1", S_IFREG, &test_file1);
  root_dir->Create("test2", S_IFREG, &test_file2);
  fbl::RefPtr<f2fs::File> vn1 = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file1));
  fbl::RefPtr<f2fs::File> vn2 = fbl::RefPtr<f2fs::File>::Downcast(std::move(test_file2));

  constexpr int kNTry = 51200;
  uint8_t buf[kPageSize * 2] = {1};
  // 3 iterations are enough to touch every block and trigger gc.
  for (int i = 0; i < 3; ++i) {
    std::thread thread1 = std::thread([&]() {
      for (int i = 0; i < kNTry; ++i) {
        size_t out_actual;
        ASSERT_EQ(vn1->Write(buf, sizeof(buf), i * 8192, &out_actual), ZX_OK);
        ASSERT_EQ(out_actual, sizeof(buf));
      }
    });

    std::thread thread2 = std::thread([&]() {
      for (int i = 0; i < kNTry; ++i) {
        size_t out_actual;
        ASSERT_EQ(vn2->Write(buf, sizeof(buf), i * 8192, &out_actual), ZX_OK);
        ASSERT_EQ(out_actual, sizeof(buf));
      }
    });

    thread1.join();
    thread2.join();
  }
  vn1->Close();
  vn2->Close();
  vn1 = nullptr;
  vn2 = nullptr;
  root_dir->Close();
  root_dir = nullptr;
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(MultiThreads, Create) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  std::string dir_name("dir");
  fbl::RefPtr<fs::Vnode> child;
  ASSERT_EQ(root_dir->Create(dir_name, S_IFDIR, &child), ZX_OK);

  fbl::RefPtr<Dir> child_dir = fbl::RefPtr<Dir>::Downcast(std::move(child));

  constexpr int kNThreads = 10;
  constexpr int kNEntries = 100;
  std::thread threads[kNThreads];
  for (auto nThread = 0; nThread < kNThreads; ++nThread) {
    threads[nThread] = std::thread([nThread, child_dir]() {
      // Create dentries more than MaxInlineDentry() to trigger ConvertInlineDir().
      for (uint32_t child_count = 0; child_count < kNEntries; ++child_count) {
        uint32_t mode = child_count % 2 == 0 ? S_IFDIR : S_IFREG;
        auto child_name = child_count + nThread * kNEntries;
        FileTester::CreateChild(child_dir.get(), mode, std::to_string(child_name));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Verify dentries.
  for (uint32_t child = 0; child < kNThreads * kNEntries; ++child) {
    fbl::RefPtr<fs::Vnode> child_vn;
    FileTester::Lookup(child_dir.get(), std::to_string(child), &child_vn);
    ASSERT_TRUE(child_vn);
    ASSERT_EQ(fbl::RefPtr<Dir>::Downcast(child_vn)->IsDir(), (child % 2) == 0);
    ASSERT_EQ(child_vn->Close(), ZX_OK);
  }

  // It should not have inline entires.
  FileTester::CheckNonInlineDir(child_dir.get());

  ASSERT_EQ(child_dir->Close(), ZX_OK);
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  child_dir = nullptr;
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(MultiThreads, Unlink) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  std::string dir_name("dir");
  fbl::RefPtr<fs::Vnode> child;
  ASSERT_EQ(root_dir->Create(dir_name, S_IFDIR, &child), ZX_OK);

  fbl::RefPtr<Dir> child_dir = fbl::RefPtr<Dir>::Downcast(std::move(child));

  constexpr int kNThreads = 10;
  constexpr int kNEntries = 100;
  std::thread threads[kNThreads];

  // create child vnodes.
  for (uint32_t child = 0; child < kNThreads * kNEntries; ++child) {
    uint32_t mode = child % 2 == 0 ? S_IFDIR : S_IFREG;
    FileTester::CreateChild(child_dir.get(), mode, std::to_string(child));
  }

  for (auto nThread = 0; nThread < kNThreads; ++nThread) {
    threads[nThread] = std::thread([nThread, child_dir]() {
      // Each thread deletes dentries to make |child_dir| empty.
      for (uint32_t child_count = 0; child_count < kNEntries; ++child_count) {
        auto child_name = child_count + nThread * kNEntries;
        bool is_dir = child_name % 2 == 0;
        FileTester::DeleteChild(child_dir.get(), std::to_string(child_name), is_dir);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // If |child_dir| is empty, it should be successful.
  FileTester::DeleteChild(root_dir.get(), dir_name);

  ASSERT_EQ(child_dir->Close(), ZX_OK);
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  child_dir = nullptr;
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

}  // namespace
}  // namespace f2fs
