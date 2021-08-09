// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>

#include "src/storage/minfs/test/unit/journal_integration_fixture.h"

namespace minfs {
namespace {

static constexpr uint8_t kFill = 0xe8;

class TruncateTest : public JournalIntegrationFixture {
 private:
  // Create a file with 2 blocks, then truncate down to 1 block. If the transaction succeeds we
  // should see the new length, but if it fails, we should still see the old length with the old
  // contents.
  void PerformOperation(Minfs* fs) {
    fbl::RefPtr<VnodeMinfs> root;
    ASSERT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);
    fbl::RefPtr<fs::Vnode> foo;
    ASSERT_EQ(root->Create("foo", 0, &foo), ZX_OK);
    auto close = fit::defer([foo]() { ASSERT_EQ(foo->Close(), ZX_OK); });
    std::vector<uint8_t> buf(kMinfsBlockSize + 10, kFill);
    size_t written;
    ASSERT_EQ(foo->Write(buf.data(), buf.size(), 0, &written), ZX_OK);
    ASSERT_EQ(written, buf.size());

    ASSERT_EQ(foo->Truncate(1), ZX_OK);
  }
};

TEST_F(TruncateTest, EnsureOldDataWhenTransactionFails) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // See the note in journal_test.cc regarding tuning these numbers.
  auto bcache = CutOffDevice(write_count() - 12 * kDiskBlocksPerFsBlock);

  // Since we cut off the transaction, we should see the old length with the old contents.
  std::unique_ptr<Minfs> fs;
  ASSERT_EQ(Minfs::Create(loop.dispatcher(), std::move(bcache), MountOptions{}, &fs), ZX_OK);

  // Open the 'foo' file.
  fbl::RefPtr<VnodeMinfs> root;
  ASSERT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);
  fbl::RefPtr<fs::Vnode> foo;
  ASSERT_EQ(root->Lookup("foo", &foo), ZX_OK);
  auto validated_options = foo->ValidateOptions(fs::VnodeConnectionOptions());
  ASSERT_TRUE(validated_options.is_ok());
  ASSERT_EQ(foo->Open(validated_options.value(), &foo), ZX_OK);
  auto close = fit::defer([foo]() { ASSERT_EQ(foo->Close(), ZX_OK); });

  // Read the file.
  std::vector<uint8_t> buf(kMinfsBlockSize + 10);
  size_t read;
  ASSERT_EQ(foo->Read(buf.data(), buf.size(), 0, &read), ZX_OK);
  ASSERT_EQ(buf.size(), read);

  // And now check the file.
  for (uint8_t c : buf) {
    EXPECT_EQ(kFill, c);
  }
}

}  // namespace
}  // namespace minfs
