// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tracing.h"

#include <lib/zircon-internal/ktrace.h>

#include <fstream>
#include <iomanip>

#include "src/lib/fxl/logging.h"

// Rewinds kernel trace buffer.
void Tracing::Rewind() {
  const auto status = zx_ktrace_control(root_resource_, KTRACE_ACTION_REWIND, 0, nullptr);
  FXL_CHECK(status == ZX_OK) << "Failed to rewind kernel trace buffer.";
}

// Starts kernel tracing.
void Tracing::Start(uint32_t group_mask) {
  const auto status = zx_ktrace_control(root_resource_, KTRACE_ACTION_START, group_mask, nullptr);
  FXL_CHECK(status == ZX_OK) << "Failed to start tracing.";

  running_ = true;
}

// Stops kernel tracing.
void Tracing::Stop() {
  const auto status = zx_ktrace_control(root_resource_, KTRACE_ACTION_STOP, 0, nullptr);
  FXL_CHECK(status == ZX_OK) << "Failed to stop tracing.";

  running_ = false;
}

// Returns a string with human-readable translations of tag name, event, and any possible flags.
std::string Tracing::InterpretTag(const uint32_t tag, const TagDefinition* info) {
  uint32_t event = KTRACE_EVENT(tag);
  uint32_t flags = KTRACE_FLAGS(tag);
  std::stringstream output;
  output << info->name << "(0x" << std::hex << event << ")";

  if (flags != 0)
    output << ", flags 0x" << std::hex << flags;

  return output.str();
}

// Writes human-readable translation for 16 byte records into file specified by <file>.
void Tracing::Write16B(std::ostream& file, const TagDefinition* info,
                       const ktrace_header_t* record) {
  file << std::dec << record->ts << ": " << InterpretTag(record->tag, info) << ", arg 0x"
       << std::hex << record->tid << "\n";
}

// Writes human-readable translation for 32 byte records into file specified by <file>.
void Tracing::Write32B(std::ostream& file, const TagDefinition* info,
                       const ktrace_rec_32b_t* record) {
  file << std::dec << record->ts << ": " << InterpretTag(record->tag, info) << ", tid 0x"
       << std::hex << record->tid << ", a 0x" << std::hex << record->a << ", b 0x" << std::hex
       << record->b << ", c 0x" << std::hex << record->c << ", d 0x" << std::hex << record->d
       << "\n";
}

// Writes human-readable translation name type records into file specified by <file>.
void Tracing::WriteName(std::ostream& file, const TagDefinition* info,
                        const ktrace_rec_name_t* record) {
  file << InterpretTag(record->tag, info) << ", id 0x" << std::hex << record->id << ", arg 0x"
       << std::hex << record->arg << ", " << record->name << "\n";
}

// Performs same action as zx_ktrace_read, but returns bytes_read.
size_t Tracing::ReadAndReturnBytesRead(zx_handle_t handle, void* data, uint32_t offset, size_t len,
                                       size_t* bytes_read) {
  const auto status = zx_ktrace_read(handle, data, offset, len, bytes_read);
  FXL_CHECK(status == ZX_OK) << "zx_ktrace_read failed.";
  return *bytes_read;
}

// Reads trace buffer and converts output into human-readable format. Stores in location defined by
// <filepath>. Will overwrite any existing files with same name.
bool Tracing::WriteHumanReadable(std::ostream& human_readable_file) {
  bool succeeded = true;

  if (running_) {
    FXL_LOG(WARNING) << "Tracing was running when human readable translation was started. Tracing "
                        "stopped.";
    Stop();
  }

  char data_buf[4096];
  size_t records_read = 0;
  size_t bytes_read = 0;
  uint32_t offset = 0;
  const auto header_size = sizeof(ktrace_header_t);

  if (!human_readable_file) {
    FXL_LOG(ERROR) << "Failed to open file.";
    return false;
  }

  while (ReadAndReturnBytesRead(root_resource_, data_buf, offset, header_size, &bytes_read) > 0) {
    // Try reading more before assuming error.
    if (bytes_read < header_size) {
      size_t bytes_read_originally = bytes_read;

      bytes_read = ReadAndReturnBytesRead(root_resource_, data_buf + bytes_read_originally,
                                          offset + bytes_read_originally,
                                          header_size - bytes_read_originally, &bytes_read);

      // Record is incomplete because it is presumably at the end of the trace buffer. Update offset
      // and redo read to make sure this is actually the case.
      if (bytes_read == 0) {
        offset += bytes_read_originally;
        continue;
      }

      bytes_read += bytes_read_originally;
    }

    // Reading less bytes than defined by ktrace_header_t can lead to reading uninitialized memory.
    if (bytes_read < header_size) {
      FXL_LOG(ERROR) << "Error reading traces, trace read stopped.";
      succeeded = false;
      break;
    }

    ktrace_header_t* record = reinterpret_cast<ktrace_header_t*>(data_buf);

    // If the record has zero length, something is wrong and the rest of the data will be junk.
    if (KTRACE_LEN(record->tag) == 0) {
      FXL_LOG(ERROR) << "Error reading traces, trace read stopped.";
      succeeded = false;
      break;
    }

    // Read trace payload.
    if (KTRACE_LEN(record->tag) > bytes_read) {
      offset += bytes_read;
      bytes_read = ReadAndReturnBytesRead(root_resource_, data_buf + bytes_read, offset,
                                          KTRACE_LEN(record->tag) - bytes_read, &bytes_read);
    }

    offset += bytes_read;
    records_read++;

    const uint32_t event = KTRACE_EVENT(record->tag);

    if (event >= countof(kTags)) {
      human_readable_file << "Unexpected event: 0x" << std::hex << event << "\n";
      continue;
    }

    const TagDefinition* info = &kTags[event];

    if (info->name == nullptr) {
      human_readable_file << "Unexpected event: 0x" << std::hex << event << "\n";
      continue;
    }
    switch (info->type) {
      case kTag16B:
        Write16B(human_readable_file, info, record);
        break;
      case kTag32B:
        Write32B(human_readable_file, info, reinterpret_cast<ktrace_rec_32b_t*>(record));
        break;
      case kTagNAME:
        WriteName(human_readable_file, info, reinterpret_cast<ktrace_rec_name_t*>(record));
        break;
      default:
        human_readable_file << "Unexpected tag type: 0x" << std::hex << info->type << "\n";
        break;
    }
  }

  human_readable_file << "\nTotal records read: " << std::dec << records_read
                      << "\nTotal bytes read: " << std::dec << offset + bytes_read << "\n";

  return succeeded;
}
