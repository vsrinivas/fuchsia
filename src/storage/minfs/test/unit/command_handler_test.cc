// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/inspector/command_handler.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <iostream>

#include <disk_inspector/vmo_buffer_factory.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;

// We choose to only test that CommandHandler can print out the supported
// commands and is able to run every command without crashing. Currently
// the actual commands are thin wrappers around other more well tested parts,
// and thus makes not much use testing them here.

TEST(MinfsCommandHandler, GetSupportedCommands) {
  CommandHandler handler(nullptr);
  std::ostringstream output_stream;
  handler.SetOutputStream(&output_stream);
  handler.PrintSupportedCommands();

  std::string expected = R"""(TogglePrintHex
	Toggles printing fields in hexadecimal.

ToggleHideArray
	Toggles showing array field entries.

PrintSuperblock
	Prints the superblock.

PrintInode [index]
	Prints an inode from the inode table.
		index: Index of inode in inode table.

PrintInodes [max]
	Prints all the inodes in the inode table
		max: Maximum number of inodes to print.

PrintAllocatedInodes [max]
	Prints all the allocated inodes in the inode table based on the inode allocation bitmap.
		max: Maximum number of allocated inodes to print.

PrintJournalSuperblock
	Prints the journal superblock.

PrintJournalEntries [max]
	Prints all the journal entries as headers, commits, revocation and unknown based on entry prefix.
		max: Maximum number of entries to print.

PrintJournalHeader [index]
	Prints a journal entry cast as a journal header.
		index: Index of journal entry to cast.

PrintJournalCommit [index]
	Prints a journal entry cast as a journal commit.
		index: Index of journal entry to cast.

PrintBackupSuperblock
	Prints the backup superblock.

WriteSuperblockField [fieldname] [value]
	Set the value of a field of the superblock to disk.
		fieldname: Name of superblock field.
		value: Value to set field.

)""";

  EXPECT_EQ(output_stream.str(), expected);
}

using TestCommands = std::vector<std::vector<std::string>>;

const TestCommands& GetTestCommands() {
  static TestCommands* test_commands = new TestCommands{{"TogglePrintHex"},
                                                        {"ToggleHideArray"},
                                                        {"PrintInode", "0"},
                                                        {"PrintInodes", "5"},
                                                        {"PrintAllocatedInodes", "5"},
                                                        {"PrintJournalSuperblock"},
                                                        {"PrintJournalEntries", "5"},
                                                        {"PrintJournalHeader", "0"},
                                                        {"PrintJournalCommit", "0"},
                                                        {"PrintBackupSuperblock"},
                                                        {"WriteSuperblockField", "magic0", "0"}};
  return *test_commands;
}

void CreateMinfsInspector(std::unique_ptr<block_client::BlockDevice> device,
                          std::unique_ptr<MinfsInspector>* out) {
  std::unique_ptr<disk_inspector::InspectorTransactionHandler> inspector_handler;
  ASSERT_EQ(disk_inspector::InspectorTransactionHandler::Create(std::move(device), kMinfsBlockSize,
                                                                &inspector_handler),
            ZX_OK);
  auto buffer_factory =
      std::make_unique<disk_inspector::VmoBufferFactory>(inspector_handler.get(), kMinfsBlockSize);
  auto result = MinfsInspector::Create(std::move(inspector_handler), std::move(buffer_factory));
  ASSERT_TRUE(result.is_ok());
  *out = result.take_value();
}
// Make sure commands don't fail when running on an unformatted device.
TEST(MinfsCommandHandler, CheckSupportedCommandsNoFail) {
  for (auto& command : GetTestCommands()) {
    auto temp = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
    std::unique_ptr<MinfsInspector> inspector;
    CreateMinfsInspector(std::move(temp), &inspector);
    CommandHandler handler(std::move(inspector));

    // Hide output since the output will mostly be garbage from using
    // an uninitialized device.
    std::ostringstream output_stream;
    handler.SetOutputStream(&output_stream);

    handler.CallCommand(command);
  }
}

// Make sure commands return OK on a formatted device.
TEST(MinfsCommandHandler, CheckSupportedCommandsSuccess) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto temp = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);

  // Format the device.
  auto bcache_or = Bcache::Create(std::move(temp), kBlockCount);
  ASSERT_TRUE(bcache_or.is_ok());
  ASSERT_TRUE(Mkfs(bcache_or.value().get()).is_ok());
  std::unique_ptr<Bcache> bcache = std::move(bcache_or.value());

  // Write journal info to the device by creating a minfs and waiting for it
  // to finish.
  MountOptions options = {};
  auto fs_or = minfs::Minfs::Create(loop.dispatcher(), std::move(bcache), options);
  ASSERT_TRUE(fs_or.is_ok());
  sync_completion_t completion;
  fs_or->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_EQ(sync_completion_wait(&completion, zx::duration::infinite().get()), ZX_OK);

  // We only care about the disk format written into the fake block device,
  // so we destroy the minfs/bcache used to format it.
  bcache = Minfs::Destroy(std::move(fs_or.value()));
  std::unique_ptr<MinfsInspector> inspector;
  CreateMinfsInspector(Bcache::Destroy(std::move(bcache)), &inspector);

  CommandHandler handler(std::move(inspector));

  // Hide outputs
  std::ostringstream output_stream;
  handler.SetOutputStream(&output_stream);

  for (const auto& command : GetTestCommands()) {
    if (command[0] == "InvalidCommand") {
      EXPECT_NE(handler.CallCommand(command), ZX_OK) << "Invalid call success?: " << command[0];
    } else {
      EXPECT_EQ(handler.CallCommand(command), ZX_OK) << "Command call failed: " << command[0];
    }
  }
}

}  // namespace
}  // namespace minfs
