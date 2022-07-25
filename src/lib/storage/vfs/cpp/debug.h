// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_DEBUG_H_
#define SRC_LIB_STORAGE_VFS_CPP_DEBUG_H_

#include <bitset>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

#include <fbl/string_buffer.h>

#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

#ifdef __Fuchsia__
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/llcpp/string_view.h>
#endif  // __Fuchsia__

// Debug-only header defining utility functions for logging flags and strings.
// May be used on both Fuchsia and host-only builds.

namespace fs {

namespace debug_internal {

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, VnodeConnectionOptions options) {
  auto make_append = [sb] {
    return [sb, first = true](const char* str) mutable {
      if (!first) {
        sb->Append(", ");
      }
      sb->Append(str);
      first = false;
    };
  };

  {
    auto append = make_append();
    sb->Append("[flags: ");
    if (options.flags.create)
      append("create");
    if (options.flags.fail_if_exists)
      append("fail_if_exists");
    if (options.flags.truncate)
      append("truncate");
    if (options.flags.directory)
      append("directory");
    if (options.flags.not_directory)
      append("not_directory");
    if (options.flags.append)
      append("append");
    if (options.flags.node_reference)
      append("node_reference");
    if (options.flags.describe)
      append("describe");
    if (options.flags.posix_write)
      append("posix_write");
    if (options.flags.posix_execute)
      append("posix_execute");
    if (options.flags.clone_same_rights)
      append("clone_same_rights");
  }

  {
    auto append = make_append();
    sb->Append(", rights: ");
    if (options.rights.read)
      append("read");
    if (options.rights.write)
      append("write");
    if (options.rights.execute)
      append("execute");
    sb->Append("]");
  }
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, const char* str) {
  sb->Append(str);
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, std::string_view view) {
  sb->Append(view.data(), view.size());
}

#ifdef __Fuchsia__

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, fidl::StringView view) {
  sb->Append(view.data(), view.size());
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, fuchsia_io::wire::NodeAttributeFlags flags) {
  constexpr std::pair<fuchsia_io::wire::NodeAttributeFlags, std::string_view> flagToString[] = {
      {fuchsia_io::wire::NodeAttributeFlags::kCreationTime, "CREATION_TIME"},
      {fuchsia_io::wire::NodeAttributeFlags::kModificationTime, "MODIFICATION_TIME"},
  };
  bool first = true;
  for (const auto& [flag, desc] : flagToString) {
    if (flags & flag) {
      if (!first) {
        sb->Append(" | ");
      }
      first = false;
      sb->Append(desc);
    }
    flags ^= flag;
  }
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, fuchsia_io::wire::OpenFlags flags) {
  constexpr std::pair<fuchsia_io::wire::OpenFlags, std::string_view> flagToString[] = {
      {fuchsia_io::wire::OpenFlags::kRightReadable, "RIGHT_READABLE"},
      {fuchsia_io::wire::OpenFlags::kRightWritable, "RIGHT_WRITABLE"},
      {fuchsia_io::wire::OpenFlags::kRightExecutable, "RIGHT_EXECUTABLE"},
      {fuchsia_io::wire::OpenFlags::kCreate, "CREATE"},
      {fuchsia_io::wire::OpenFlags::kCreateIfAbsent, "CREATE_IF_ABSENT"},
      {fuchsia_io::wire::OpenFlags::kTruncate, "TRUNCATE"},
      {fuchsia_io::wire::OpenFlags::kDirectory, "DIRECTORY"},
      {fuchsia_io::wire::OpenFlags::kAppend, "APPEND"},
      {fuchsia_io::wire::OpenFlags::kNodeReference, "NODE_REFERENCE"},
      {fuchsia_io::wire::OpenFlags::kDescribe, "DESCRIBE"},
      {fuchsia_io::wire::OpenFlags::kPosixWritable, "POSIX_WRITABLE"},
      {fuchsia_io::wire::OpenFlags::kPosixExecutable, "POSIX_EXECUTABLE"},
      {fuchsia_io::wire::OpenFlags::kNotDirectory, "NOT_DIRECTORY"},
      {fuchsia_io::wire::OpenFlags::kCloneSameRights, "CLONE_SAME_RIGHTS"},
  };
  bool first = true;
  for (const auto& [flag, desc] : flagToString) {
    if (flags & flag) {
      if (!first) {
        sb->Append(" | ");
      }
      first = false;
      sb->Append(desc);
    }
    flags ^= flag;
  }
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, fuchsia_io::wire::VmoFlags flags) {
  constexpr std::pair<fuchsia_io::wire::VmoFlags, std::string_view> flagToString[] = {
      {fuchsia_io::wire::VmoFlags::kRead, "READ"},
      {fuchsia_io::wire::VmoFlags::kWrite, "WRITE"},
      {fuchsia_io::wire::VmoFlags::kExecute, "EXECUTE"},
      {fuchsia_io::wire::VmoFlags::kPrivateClone, "PRIVATE_CLONE"},
      {fuchsia_io::wire::VmoFlags::kSharedBuffer, "SHARED_BUFFER"},
  };
  bool first = true;
  for (const auto& [flag, desc] : flagToString) {
    if (flags & flag) {
      if (!first) {
        sb->Append(" | ");
      }
      first = false;
      sb->Append(desc);
    }
    flags ^= flag;
  }
}

#endif  // __Fuchsia__

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, uint32_t num) {
  sb->AppendPrintf("%u", num);
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, void* p) {
  sb->AppendPrintf("%p", p);
}

template <size_t N>
void PrintEach(fbl::StringBuffer<N>* sb) {}

template <size_t N, typename T, typename... Args>
void PrintEach(fbl::StringBuffer<N>* sb, T val, Args... args) {
  PrintIntoStringBuffer(sb, val);
  PrintEach(sb, args...);
}

void Log(std::string_view buffer);

template <typename... Args>
void ConnectionTraceDebug(Args... args) {
  constexpr size_t kMaxSize = 2000;
  auto str = std::make_unique<fbl::StringBuffer<kMaxSize>>();
  PrintEach(str.get(), args...);
  Log(*str);
}

}  // namespace debug_internal

}  // namespace fs

#define FS_PRETTY_TRACE_DEBUG(args...)                \
  do {                                                \
    if (fs::trace_debug_enabled())                    \
      fs::debug_internal::ConnectionTraceDebug(args); \
  } while (0)

#endif  // SRC_LIB_STORAGE_VFS_CPP_DEBUG_H_
