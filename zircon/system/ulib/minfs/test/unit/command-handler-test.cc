// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minfs/command-handler.h"

#include <iostream>

#include <block-client/cpp/fake-device.h>
#include <minfs/format.h>
#include <zxtest/zxtest.h>

#include "minfs-private.h"

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

  std::string expected = R"""(ToggleUseHex
ToggleHideArray
PrintSuperblock
PrintInode [index]
PrintInodes [max]
PrintAllocatedInodes [max]
PrintJournalSuperblock
PrintJournalEntries [max]
PrintJournalHeader [index]
PrintJournalCommit [index]
PrintBackupSuperblock
)""";

  EXPECT_STR_EQ(output_stream.str().c_str(), expected.c_str());
}

std::vector<std::vector<std::string>> test_commands = {{"InvalidCommand", "1", "2", "3"},
                                                       {"ToggleUseHex"},
                                                       {"ToggleHideArray"},
                                                       {"PrintInode", "0"},
                                                       {"PrintInodes", "5"},
                                                       {"PrintAllocatedInodes", "5"},
                                                       {"PrintJournalSuperblock"},
                                                       {"PrintJournalEntries", "5"},
                                                       {"PrintJournalHeader", "0"},
                                                       {"PrintJournalCommit", "0"},
                                                       {"PrintBackupSuperblock"}};

// Make sure commands don't fail when running on an unformatted device.
TEST(MinfsCommandHandler, CheckSupportedCommandsNoFail) {
  for (auto command : test_commands) {
    ASSERT_NO_DEATH(
        [&command]() {
          auto temp = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
          std::unique_ptr<MinfsInspector> inspector;
          ASSERT_OK(MinfsInspector::Create(std::move(temp), &inspector));
          CommandHandler handler(std::move(inspector));

          // Hide output since the output will mostly be garbage from using
          // an uninitialized device.
          std::ostringstream output_stream;
          handler.SetOutputStream(&output_stream);

          handler.CallCommand(command);
        },
        "Failed test calling command: %s\n", command[0].c_str());
  }
}

// Make sure commands return OK on a formatted device.
TEST(MinfsCommandHandler, CheckSupportedCommandsSuccess) {
  auto temp = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);

  // Format the device.
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(temp), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));

  // Write journal info to the device by creating a minfs and waiting for it
  // to finish.
  std::unique_ptr<Minfs> fs;
  MountOptions options = {};
  ASSERT_OK(minfs::Minfs::Create(std::move(bcache), options, &fs));
  sync_completion_t completion;
  fs->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));

  // We only care about the disk format written into the fake block device,
  // so we destroy the minfs/bcache used to format it.
  bcache = Minfs::Destroy(std::move(fs));
  std::unique_ptr<MinfsInspector> inspector;
  ASSERT_OK(MinfsInspector::Create(Bcache::Destroy(std::move(bcache)), &inspector));

  CommandHandler handler(std::move(inspector));

  // Hide outputs
  std::ostringstream output_stream;
  handler.SetOutputStream(&output_stream);

  for (const auto& command : test_commands) {
    if (command[0] == "InvalidCommand") {
      EXPECT_NOT_OK(handler.CallCommand(command), "Invalid call success?: %s\n", command[0].data());
    } else {
      EXPECT_OK(handler.CallCommand(command), "Command call failed: %s\n", command[0].data());
    }
  }
}

}  // namespace
}  // namespace minfs
