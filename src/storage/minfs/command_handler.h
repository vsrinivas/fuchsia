// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_COMMAND_HANDLER_H_
#define SRC_STORAGE_MINFS_COMMAND_HANDLER_H_

#include <iostream>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include <block-client/cpp/block-device.h>
#include <disk_inspector/command.h>
#include <disk_inspector/command_handler.h>
#include <disk_inspector/common_types.h>
#include <disk_inspector/disk_struct.h>
#include <disk_inspector/inspector_transaction_handler.h>
#include <storage/buffer/vmo_buffer.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_inspector.h"

namespace minfs {
using ArgType = disk_inspector::ArgType;
using FieldType = disk_inspector::FieldType;

// CommandHandler for Minfs commands.
class CommandHandler : public disk_inspector::CommandHandler {
 public:
  explicit CommandHandler(std::unique_ptr<MinfsInspector> inspector)
      : inspector_(std::move(inspector)) {
    InitializeCommands();
  }

  // disk_inspector::CommandHandler interface:
  void PrintSupportedCommands() final;
  zx_status_t CallCommand(std::vector<std::string> command) final;

  // Function to allow customization where output is redirected.
  void SetOutputStream(std::ostream* stream) { output_ = stream; }

 private:
  void InitializeCommands();

  // Commands: we are leaving them in the CommandHandler directly for now
  // before the number of commands becomes large and the need to separate them
  // out becomes clear.

  // Toggles whether future prints calls to display hex numbers for field values.
  zx_status_t TogglePrintHex();

  // Toggles whether future print calls will display the full array for array fields.
  zx_status_t ToggleHideArray();

  // Prints the minfs superblock to |output_|.
  zx_status_t PrintSuperblock();

  // Prints the inode at |index| to |output_|.
  zx_status_t PrintInode(uint64_t index);

  // Prints every inode in the inode table in order to |output_|. |max| represents
  // the number of entries to print if |max| is less the the total number of
  // entries.
  zx_status_t PrintInodes(uint64_t max);

  // Prints inodes defined as allocated in the inode allocation bitmap
  // in order to |output_|. |max| represents the number of entries to print
  // if |max| is less the the total number of entries.
  zx_status_t PrintAllocatedInodes(uint64_t max);

  // Prints the JournalInfo object to |output_|.
  zx_status_t PrintJournalSuperblock();

  // Prints to |output_| every JournalEntry block in order by first getting
  // the prefix at each block to check if it is a header, commit, recovation,
  // or payload and printing based on the specific format. |max| represents
  // the number of entries to print if |max| is less the the total number of
  // entries.
  zx_status_t PrintJournalEntries(uint64_t max);

  // Prints the journal entry at |index| as a JournalHeader struct to |output_|.
  zx_status_t PrintJournalHeader(uint64_t index);

  // Prints the jouranl entry at |index| as a JournalCommit struct to |output_|.
  zx_status_t PrintJournalCommit(uint64_t index);

  // Prints the minfs backup superblock to |output_|.
  zx_status_t PrintBackupSuperblock();

  // Gets the superblock from the inspector, edits the field with |fieldname|
  // to be |value|, and writes the superblock to disk.
  zx_status_t WriteSuperblockField(std::string fieldname, std::string value);

  std::unique_ptr<MinfsInspector> inspector_;
  std::vector<disk_inspector::Command> command_list_;
  // |name_to_index_| is a mapping of the string name of the command to the
  // uint64_t index of the associated disk_inspector::Command in |command_list_|.
  std::map<std::string, uint64_t> name_to_index_;
  disk_inspector::PrintOptions options_ = {.display_hex = false, .hide_array = true};
  std::ostream* output_ = &std::cout;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_COMMAND_HANDLER_H_
