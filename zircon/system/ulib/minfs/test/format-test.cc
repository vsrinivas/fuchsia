// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests minfs format behavior.

#include <zxtest/zxtest.h>

#include <minfs/format.h>

namespace minfs {
namespace {

TEST(MinfsFormat, MinfsSuperblock) {
    minfs::Superblock info = {};
    info.block_count = 29;
    info.ibm_block = 2;
    info.abm_block = 3;
    info.ino_block = 5;
    info.journal_start_block = 11;
    info.dat_block = 19;

    info.ibm_slices = 3;
    info.abm_slices = 5;
    info.ino_slices = 11;
    info.journal_slices = 13;
    info.dat_slices = 17;

    ASSERT_FALSE(GetMinfsFlagFvm(info));

    ASSERT_EQ(InodeBitmapBlocks(info), info.abm_block - info.ibm_block);

    ASSERT_EQ(BlockBitmapBlocks(info), info.ino_block - info.abm_block);

    ASSERT_EQ(InodeBlocks(info), info.journal_start_block - info.ino_block);

    ASSERT_EQ(JournalBlocks(info), info.dat_block - info.journal_start_block);

    ASSERT_EQ(DataBlocks(info), info.block_count);

    ASSERT_EQ(NonDataBlocks(info), InodeBitmapBlocks(info) + BlockBitmapBlocks(info) +
                                       InodeBlocks(info) + JournalBlocks(info));
}

TEST(MinfsFormat, MinfsSuperblockOnFvm) {
    minfs::Superblock info = {};
    info.block_count = 29;
    info.ibm_block = 2;
    info.abm_block = 3;
    info.ino_block = 5;
    info.journal_start_block = 11;
    info.dat_block = 19;

    info.ibm_slices = 3;
    info.abm_slices = 5;
    info.ino_slices = 11;
    info.journal_slices = 13;
    info.dat_slices = 17;

    SetMinfsFlagFvm(info);
    ASSERT_TRUE(GetMinfsFlagFvm(info));

    ASSERT_EQ(InodeBitmapBlocks(info), info.ibm_slices);

    ASSERT_EQ(BlockBitmapBlocks(info), info.abm_slices);

    ASSERT_EQ(InodeBlocks(info), info.ino_slices);

    ASSERT_EQ(JournalBlocks(info), info.journal_slices);

    ASSERT_EQ(DataBlocks(info), info.dat_slices);

    ASSERT_EQ(NonDataBlocks(info), InodeBitmapBlocks(info) + BlockBitmapBlocks(info) +
                                       InodeBlocks(info) + JournalBlocks(info));
}

} // namespace
} // namespace minfs
