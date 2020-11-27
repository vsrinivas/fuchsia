// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/command_handler.h"

#include <zircon/errors.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

#include <disk_inspector/disk_struct.h>
#include <fs/journal/disk_struct.h>

#include "src/storage/minfs/inspector/disk_struct.h"

namespace minfs {

using ParsedCommand = disk_inspector::ParsedCommand;

void CommandHandler::PrintSupportedCommands() {
  *output_ << disk_inspector::PrintCommandList(command_list_);
}

zx_status_t CommandHandler::CallCommand(std::vector<std::string> command_args) {
  if (command_args.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }
  std::string command_name = command_args[0];
  auto command_index = name_to_index_.find(command_name);
  if (command_index == name_to_index_.end()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  const disk_inspector::Command& command = command_list_[command_index->second];
  auto fit_result = disk_inspector::ParseCommand(command_args, command);
  if (fit_result.is_error()) {
    std::ostringstream os;
    os << "Usage: " << disk_inspector::PrintCommand(command);
    os << "\n";
    std::cerr << os.str();
    return fit_result.take_error();
  }
  ParsedCommand args = fit_result.take_value();
  return command.function(std::move(args));
}

void CommandHandler::InitializeCommands() {
  command_list_ = {
      {"TogglePrintHex",
       {},
       "Toggles printing fields in hexadecimal.",
       [this](ParsedCommand args) -> zx_status_t { return TogglePrintHex(); }},

      {"ToggleHideArray",
       {},
       "Toggles showing array field entries.",
       [this](ParsedCommand args) -> zx_status_t { return ToggleHideArray(); }},

      {"PrintSuperblock",
       {},
       "Prints the superblock.",
       [this](ParsedCommand args) -> zx_status_t { return PrintSuperblock(); }},

      {"PrintInode",
       {
           {"index", ArgType::kUint64, "Index of inode in inode table."},
       },
       "Prints an inode from the inode table.",
       [this](ParsedCommand args) -> zx_status_t {
         return PrintInode(args.uint64_fields["index"]);
       }},

      {"PrintInodes",
       {
           {"max", ArgType::kUint64, "Maximum number of inodes to print."},
       },
       "Prints all the inodes in the inode table",
       [this](ParsedCommand args) -> zx_status_t {
         return PrintInodes(args.uint64_fields["max"]);
       }},

      {"PrintAllocatedInodes",
       {
           {"max", ArgType::kUint64, "Maximum number of allocated inodes to print."},
       },
       "Prints all the allocated inodes in the inode table based on the inode allocation bitmap.",
       [this](ParsedCommand args) -> zx_status_t {
         return PrintAllocatedInodes(args.uint64_fields["max"]);
       }},

      {"PrintJournalSuperblock",
       {},
       "Prints the journal superblock.",
       [this](ParsedCommand args) -> zx_status_t { return PrintJournalSuperblock(); }},

      {"PrintJournalEntries",
       {
           {"max", ArgType::kUint64, "Maximum number of entries to print."},
       },
       "Prints all the journal entries as headers, commits, revocation and unknown based on entry "
       "prefix.",
       [this](ParsedCommand args) -> zx_status_t {
         return PrintJournalEntries(args.uint64_fields["max"]);
       }},

      {"PrintJournalHeader",
       {
           {"index", ArgType::kUint64, "Index of journal entry to cast."},
       },
       "Prints a journal entry cast as a journal header.",
       [this](ParsedCommand args) -> zx_status_t {
         return PrintJournalHeader(args.uint64_fields["index"]);
       }},

      {"PrintJournalCommit",
       {
           {"index", ArgType::kUint64, "Index of journal entry to cast."},
       },
       "Prints a journal entry cast as a journal commit.",
       [this](ParsedCommand args) -> zx_status_t {
         return PrintJournalCommit(args.uint64_fields["index"]);
       }},

      {"PrintBackupSuperblock",
       {},
       "Prints the backup superblock.",
       [this](ParsedCommand args) -> zx_status_t { return PrintBackupSuperblock(); }},

      {"WriteSuperblockField",
       {
           {"fieldname", ArgType::kString, "Name of superblock field."},
           {"value", ArgType::kString, "Value to set field."},
       },
       "Set the value of a field of the superblock to disk.",
       [this](ParsedCommand args) -> zx_status_t {
         return WriteSuperblockField(args.string_fields["fieldname"], args.string_fields["value"]);
       }},
  };

  for (uint64_t i = 0; i < command_list_.size(); ++i) {
    name_to_index_[command_list_[i].name] = i;
  }
}

zx_status_t CommandHandler::TogglePrintHex() {
  options_.display_hex = !options_.display_hex;
  if (options_.display_hex) {
    *output_ << "Displaying numbers as hexadecimal.\n";
  } else {
    *output_ << "Displaying numbers in base 10.\n";
  }
  return ZX_OK;
}

zx_status_t CommandHandler::ToggleHideArray() {
  options_.hide_array = !options_.hide_array;
  if (options_.hide_array) {
    *output_ << "Hiding array elements on print.\n";
  } else {
    *output_ << "Showing array elements on print.\n";
  }
  return ZX_OK;
}

zx_status_t CommandHandler::PrintSuperblock() {
  Superblock superblock = inspector_->InspectSuperblock();
  std::unique_ptr<disk_inspector::DiskStruct> object = GetSuperblockStruct();
  *output_ << object->ToString(&superblock, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintInode(uint64_t index) {
  auto result = inspector_->InspectInodeRange(index, index + 1);
  if (result.is_error()) {
    return result.take_error();
  }
  Inode inode = result.take_value()[0];
  std::unique_ptr<disk_inspector::DiskStruct> object = GetInodeStruct(index);
  *output_ << object->ToString(&inode, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintInodes(uint64_t max) {
  uint64_t count = std::min(max, inspector_->GetInodeCount());
  if (count == 0) {
    return ZX_OK;
  }
  auto result = inspector_->InspectInodeRange(0, count);
  if (result.is_error()) {
    return result.take_error();
  }
  std::vector<Inode> inodes = result.take_value();
  for (uint64_t i = 0; i < count; ++i) {
    Inode inode = inodes[i];
    std::unique_ptr<disk_inspector::DiskStruct> object = GetInodeStruct(i);
    *output_ << object->ToString(&inode, options_);
  }
  return ZX_OK;
}

zx_status_t CommandHandler::PrintAllocatedInodes(uint64_t max) {
  uint64_t count = inspector_->GetInodeCount();
  if (count == 0) {
    return ZX_OK;
  }
  auto result = inspector_->InspectInodeAllocatedInRange(0, count);
  if (result.is_error()) {
    return result.take_error();
  }

  std::vector<uint64_t> allocated_indices = result.take_value();
  if (allocated_indices.size() > max) {
    allocated_indices.resize(max);
  }
  for (uint64_t allocated_index : allocated_indices) {
    PrintInode(allocated_index);
  }
  return ZX_OK;
}

zx_status_t CommandHandler::PrintJournalSuperblock() {
  auto result = inspector_->InspectJournalSuperblock();
  if (result.is_error()) {
    return result.take_error();
  }
  fs::JournalInfo info = result.take_value();
  std::unique_ptr<disk_inspector::DiskStruct> object = fs::GetJournalSuperblockStruct();
  *output_ << object->ToString(&info, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintJournalEntries(uint64_t max) {
  uint64_t count = std::min(max, inspector_->GetJournalEntryCount());
  for (uint64_t i = 0; i < count; ++i) {
    auto result = inspector_->InspectJournalEntryAs<fs::JournalPrefix>(i);
    if (result.is_error()) {
      return result.take_error();
    }
    fs::JournalPrefix prefix = result.take_value();
    switch (prefix.ObjectType()) {
      case fs::JournalObjectType::kHeader: {
        PrintJournalHeader(i);
        break;
      }
      case fs::JournalObjectType::kCommit: {
        PrintJournalCommit(i);
        break;
      }
      case fs::JournalObjectType::kRevocation: {
        *output_ << "Name: Journal Revocation, Block #" << i << "\n";
        break;
      }
      default: {
        *output_ << "Name: Journal Unknown, Block #" << i << "\n";
        break;
      }
    }
  }
  return ZX_OK;
}

zx_status_t CommandHandler::PrintJournalHeader(uint64_t index) {
  auto result = inspector_->InspectJournalEntryAs<fs::JournalHeaderBlock>(index);
  if (result.is_error()) {
    return result.take_error();
  }
  fs::JournalHeaderBlock header = result.take_value();
  std::unique_ptr<disk_inspector::DiskStruct> object = fs::GetJournalHeaderBlockStruct(index);
  *output_ << object->ToString(&header, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintJournalCommit(uint64_t index) {
  auto result = inspector_->InspectJournalEntryAs<fs::JournalCommitBlock>(index);
  if (result.is_error()) {
    return result.take_error();
  }
  fs::JournalCommitBlock commit = result.take_value();
  std::unique_ptr<disk_inspector::DiskStruct> object = fs::GetJournalCommitBlockStruct(index);
  *output_ << object->ToString(&commit, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintBackupSuperblock() {
  auto result = inspector_->InspectBackupSuperblock();
  if (result.is_error()) {
    return result.take_error();
  }
  Superblock superblock = result.take_value();
  std::unique_ptr<disk_inspector::DiskStruct> object = GetSuperblockStruct();
  *output_ << object->ToString(&superblock, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::WriteSuperblockField(std::string fieldname, std::string value) {
  Superblock superblock = inspector_->InspectSuperblock();
  std::unique_ptr<disk_inspector::DiskStruct> object = GetSuperblockStruct();
  zx_status_t status = object->WriteField(&superblock, {std::move(fieldname)}, {0}, value);
  if (status != ZX_OK) {
    return status;
  }
  auto result = inspector_->WriteSuperblock(superblock);
  if (result.is_error()) {
    return result.take_error();
  }
  return ZX_OK;
}

}  // namespace minfs
