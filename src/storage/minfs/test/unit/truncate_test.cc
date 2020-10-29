// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

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
    ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));
    fbl::RefPtr<fs::Vnode> foo;
    ASSERT_OK(root->Create("foo", 0, &foo));
    auto close = fbl::MakeAutoCall([foo]() { ASSERT_OK(foo->Close()); });
    std::vector<uint8_t> buf(kMinfsBlockSize + 10, kFill);
    size_t written;
    ASSERT_OK(foo->Write(buf.data(), buf.size(), 0, &written));
    ASSERT_EQ(written, buf.size());

    ASSERT_OK(foo->Truncate(1));
  }
};

TEST_F(TruncateTest, EnsureOldDataWhenTransactionFails) {
  // See the note in journal_test.cc regarding tuning these numbers.
  auto bcache = CutOffDevice(write_count() - 12 * kDiskBlocksPerFsBlock);

  // Since we cut off the transaction, we should see the old length with the old contents.
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), MountOptions{}, &fs));

  // Open the 'foo' file.
  fbl::RefPtr<VnodeMinfs> root;
  ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));
  fbl::RefPtr<fs::Vnode> foo;
  ASSERT_OK(root->Lookup("foo", &foo));
  auto validated_options = foo->ValidateOptions(fs::VnodeConnectionOptions());
  ASSERT_TRUE(validated_options.is_ok());
  ASSERT_OK(foo->Open(validated_options.value(), &foo));
  auto close = fbl::MakeAutoCall([foo]() { ASSERT_OK(foo->Close()); });

  // Read the file.
  std::vector<uint8_t> buf(kMinfsBlockSize + 10);
  size_t read;
  ASSERT_OK(foo->Read(buf.data(), buf.size(), 0, &read));
  ASSERT_EQ(buf.size(), read);

  // And now check the file.
  for (uint8_t c : buf) {
    EXPECT_EQ(kFill, c);
  }
}

}  // namespace
}  // namespace minfs
