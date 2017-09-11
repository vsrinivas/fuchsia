// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt_snoop_logger.h"

#include <cstring>

#include <endian.h>
#include <fcntl.h>
#include <magenta/compiler.h>

#include "lib/fxl/files/file.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"

#include "byte_buffer.h"

namespace bluetooth {
namespace common {
namespace {

// The BTSnoop file header fields.
const char kIdPattern[] = {'b', 't', 's', 'n', 'o', 'o', 'p', '\0'};
constexpr uint32_t kVersionNumber = 1;
constexpr uint32_t kDataLinkType = 1001;  // Un-encapsulated HCI (H1)

// The BTSnoop epoch is defined as "midnight, January 1st, 0 AD nominal
// Gregorian". This is the number of microseconds between the BTSnoop epoch and
// midnight 1/1/1970.
constexpr int64_t kEpochDelta = 0x00dcddb30f2f8000ll;

struct Header {
  char id_pattern[sizeof(kIdPattern)];
  uint32_t version;
  uint32_t data_link_type;
} __PACKED;

struct RecordHeader {
  uint32_t original_length;
  uint32_t included_length;
  uint32_t packet_flags;
  uint32_t cumulative_drops;
  int64_t timestamp_ms;
} __PACKED;

// TODO(armansito): Casting data to "const char*" is weird. Should
// fxl::WriteFileDescriptor be changed to accept "const void*" instead? It's
// also weird that it expect "ssize_t" for its argument. When would anyone pass
// a negative number to it? Also does our code even need to worry about EINTR?
inline bool WriteToFile(const fxl::UniqueFD& fd, const void* data, size_t size) {
  return fxl::WriteFileDescriptor(fd.get(), static_cast<const char*>(data), size);
}

bool WriteHeader(const fxl::UniqueFD& fd) {
  FXL_DCHECK(fd.is_valid());

  Header header;
  std::memcpy(header.id_pattern, kIdPattern, sizeof(kIdPattern));
  header.version = htobe32(kVersionNumber);
  header.data_link_type = htobe32(kDataLinkType);

  return WriteToFile(fd, &header, sizeof(header));
}

bool WriteRecordHeader(const fxl::UniqueFD& fd, size_t packet_size, bool is_received,
                       bool is_data) {
  RecordHeader header;
  memset(&header, 0, sizeof(header));

  header.original_length = htobe32(packet_size);
  header.included_length = htobe32(packet_size);

  if (is_received) header.packet_flags |= 0x01;
  if (!is_data) header.packet_flags |= 0x02;
  header.packet_flags = htobe32(header.packet_flags);

  auto time_delta = fxl::TimePoint::Now().ToEpochDelta();
  header.timestamp_ms = time_delta.ToMicroseconds();
  header.timestamp_ms += kEpochDelta;
  header.timestamp_ms = htobe64(header.timestamp_ms);

  return WriteToFile(fd, &header, sizeof(header));
}

}  // namespace

bool BTSnoopLogger::Initialize(const std::string& path, bool truncate) {
  if (fd_.is_valid()) {
    FXL_VLOG(1) << "BTSnoop logger already initialized";
    return false;
  }

  int oflags = O_SYNC | O_CREAT | O_WRONLY;
  if (truncate) oflags |= O_TRUNC;

  fxl::UniqueFD fd(open(path.c_str(), oflags));
  if (!fd.is_valid()) {
    FXL_LOG(ERROR) << "Failed to initialize BTSnoop log file";
    return false;
  }

  size_t file_size = 0;
  if (!files::GetFileSize(path, &file_size)) {
    FXL_LOG(ERROR) << "Failed to determine file size";
    return false;
  }

  // Write the header only if the file is empty.
  if (!file_size && !WriteHeader(fd)) {
    FXL_LOG(ERROR) << "Failed to write BTSnoop header";
    return false;
  }

  fd_ = std::move(fd);

  return true;
}

bool BTSnoopLogger::WritePacket(const ByteBuffer& packet_data, bool is_received, bool is_data) {
  if (!fd_.is_valid()) {
    FXL_LOG(ERROR) << "BTSnoop logger not initialized";
    return false;
  }

  if (!WriteRecordHeader(fd_, packet_data.size(), is_received, is_data)) {
    FXL_LOG(ERROR) << "Failed to write BTSnoop record header";
    return false;
  }

  if (!WriteToFile(fd_, packet_data.data(), packet_data.size())) {
    FXL_LOG(ERROR) << "Failed to write BTSnoop record packet data";
    // TODO(armansito): The file contents are now malformed. Seek back to the
    // beginning of the record header?
    return false;
  }

  return true;
}

}  // namespace common
}  // namespace bluetooth
