// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minfs/command-handler.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

#include <disk_inspector/disk_struct.h>
#include <fs/journal/disk_struct.h>
#include <fs/trace.h>

#include "disk-struct.h"

namespace minfs {

zx_status_t CommandHandler::Create(std::unique_ptr<block_client::BlockDevice> device,
                                   std::unique_ptr<disk_inspector::CommandHandler>* out) {
  std::unique_ptr<MinfsInspector> inspector;
  zx_status_t status = MinfsInspector::Create(std::move(device), &inspector);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot create minfs command handler.\n");
    return status;
  }
  *out = std::make_unique<CommandHandler>(std::move(inspector));
  return ZX_OK;
}

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
  disk_inspector::Command command = command_list_[command_index->second];
  auto fit_result = disk_inspector::ParseCommand(command_args, command);
  if (fit_result.is_error()) {
    std::ostringstream os;
    os << "Usage: " << disk_inspector::PrintCommand(command);
    os << "\n";
    std::cerr << os.str();
    return fit_result.take_error();
  }
  // TODO(fxb/47553) Add function as a part of the Command struct and handle
  // getting arguments there to eliminate this switch statement and make the
  // code cleaner.
  disk_inspector::ParsedCommand args = fit_result.take_ok_result().value;
  zx_status_t status = ZX_OK;
  if (command_name == "ToggleUseHex") {
    status = TogglePrintHex();
  } else if (command_name == "ToggleHideArray") {
    status = ToggleHideArray();
  } else if (command_name == "PrintSuperblock") {
    status = PrintSuperblock();
  } else if (command_name == "PrintInode") {
    status = PrintInode(args.uint64_fields["index"]);
  } else if (command_name == "PrintInodes") {
    status = PrintInodes(args.uint64_fields["max"]);
  } else if (command_name == "PrintAllocatedInodes") {
    status = PrintAllocatedInodes(args.uint64_fields["max"]);
  } else if (command_name == "PrintJournalSuperblock") {
    status = PrintJournalSuperblock();
  } else if (command_name == "PrintJournalEntries") {
    status = PrintJournalEntries(args.uint64_fields["max"]);
  } else if (command_name == "PrintJournalHeader") {
    status = PrintJournalHeader(args.uint64_fields["index"]);
  } else if (command_name == "PrintJournalCommit") {
    status = PrintJournalCommit(args.uint64_fields["index"]);
  } else if (command_name == "PrintBackupSuperblock") {
    status = PrintBackupSuperblock();
  }
  return status;
}

void CommandHandler::InitializeCommands() {
  command_list_ = {
      {"ToggleUseHex", {}},
      {"ToggleHideArray", {}},
      {"PrintSuperblock", {}},
      {"PrintInode",
       {
           {
               "index",
               ArgType::kUint64,
           },
       }},
      {"PrintInodes",
       {
           {
               "max",
               ArgType::kUint64,
           },
       }},
      {"PrintAllocatedInodes",
       {
           {
               "max",
               ArgType::kUint64,
           },
       }},
      {"PrintJournalSuperblock", {}},
      {"PrintJournalEntries",
       {
           {
               "max",
               ArgType::kUint64,
           },
       }},
      {"PrintJournalHeader",
       {
           {
               "index",
               ArgType::kUint64,
           },
       }},
      {"PrintJournalCommit",
       {
           {
               "index",
               ArgType::kUint64,
           },
       }},
      {"PrintBackupSuperblock", {}},
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
  uint64_t inode_count = inspector_->GetInodeCount();
  // TODO(fxb/47554): Remove this check once minfs-inspector stops caching
  // this data and does not error if receiving an invalid index.
  if (index >= inode_count) {
    *output_ << "Index outside range of valid inodes. Total: " << inode_count << "\n";
    return ZX_ERR_INVALID_ARGS;
  }
  Inode inode = inspector_->InspectInode(index);
  std::unique_ptr<disk_inspector::DiskStruct> object = GetInodeStruct(index);
  *output_ << object->ToString(&inode, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintInodes(uint64_t max) {
  uint64_t count = std::min(max, inspector_->GetInodeCount());
  for (uint64_t i = 0; i < count; ++i) {
    PrintInode(i);
  }
  return ZX_OK;
}

zx_status_t CommandHandler::PrintAllocatedInodes(uint64_t max) {
  uint64_t count = 0;
  for (uint64_t i = 0; i < inspector_->GetInodeBitmapCount(); ++i) {
    if (inspector_->CheckInodeAllocated(i)) {
      PrintInode(i);
      count++;
      if (count >= max) {
        break;
      }
    }
  }
  return ZX_OK;
}

zx_status_t CommandHandler::PrintJournalSuperblock() {
  fs::JournalInfo info = inspector_->InspectJournalSuperblock();
  std::unique_ptr<disk_inspector::DiskStruct> object = fs::GetJournalSuperblockStruct();
  *output_ << object->ToString(&info, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintJournalEntries(uint64_t max) {
  uint64_t count = std::min(max, inspector_->GetJournalEntryCount());
  for (uint64_t i = 0; i < count; ++i) {
    fs::JournalPrefix prefix = inspector_->InspectJournalPrefix(i);
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
  uint64_t count = inspector_->GetJournalEntryCount();
  // TODO(fxb/47554): Remove this check once minfs-inspector stops caching
  // this data and does not error if receiving an invalid index.
  if (index >= count) {
    *output_ << "Index outside range of valid entries. Total: " << count << "\n";
    return ZX_ERR_INVALID_ARGS;
  }
  fs::JournalHeaderBlock header = inspector_->InspectJournalHeader(index);
  std::unique_ptr<disk_inspector::DiskStruct> object = fs::GetJournalHeaderBlockStruct(index);
  *output_ << object->ToString(&header, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintJournalCommit(uint64_t index) {
  uint64_t count = inspector_->GetJournalEntryCount();
  // TODO(fxb/47554): Remove this check once minfs-inspector stops caching
  // this data and does not error if receiving an invalid index.
  if (index >= count) {
    *output_ << "Index outside range of valid entries. Total: " << count << "\n";
    return ZX_ERR_INVALID_ARGS;
  }
  fs::JournalCommitBlock commit = inspector_->InspectJournalCommit(index);
  std::unique_ptr<disk_inspector::DiskStruct> object = fs::GetJournalCommitBlockStruct(index);
  *output_ << object->ToString(&commit, options_);
  return ZX_OK;
}

zx_status_t CommandHandler::PrintBackupSuperblock() {
  Superblock superblock;
  zx_status_t status = inspector_->InspectBackupSuperblock(&superblock);
  if (status != ZX_OK) {
    *output_ << "Cannot get backup superblock. err: " << status << "\n";
    return status;
  }
  std::unique_ptr<disk_inspector::DiskStruct> object = GetSuperblockStruct();
  *output_ << object->ToString(&superblock, options_);
  return ZX_OK;
}

}  // namespace minfs
