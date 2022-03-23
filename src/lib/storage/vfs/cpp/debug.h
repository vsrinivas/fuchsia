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

// Debug-only header defining utility functions for logging flags and paths.
// May be used on both Fuchsia and host-only builds.

namespace fs {

// Marker type for pretty-printing flags
struct ZxFlags {
 public:
  explicit ZxFlags(uint32_t flags) : value(flags) {}
  uint32_t value;
};

struct Path {
  Path(const char* path, size_t size) : str(path), size(size) {}
  const char* str;
  size_t size;
};

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
    if (options.flags.no_remote)
      append("no_remote");
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
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, Path path) {
  sb->Append(path.str, path.size);
}

#ifdef __Fuchsia__

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, fidl::StringView path) {
  sb->Append(path.data(), path.size());
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

constexpr const char* FlagToString(uint32_t flag) {
  switch (flag) {
    case fuchsia_io::wire::kOpenRightReadable:
      return "RIGHT_READABLE";
    case fuchsia_io::wire::kOpenRightWritable:
      return "RIGHT_WRITABLE";
    case fuchsia_io::wire::kOpenRightExecutable:
      return "RIGHT_EXECUTABLE";
    case fuchsia_io::wire::kOpenFlagCreate:
      return "CREATE";
    case fuchsia_io::wire::kOpenFlagCreateIfAbsent:
      return "CREATE_IF_ABSENT";
    case fuchsia_io::wire::kOpenFlagTruncate:
      return "TRUNCATE";
    case fuchsia_io::wire::kOpenFlagDirectory:
      return "DIRECTORY";
    case fuchsia_io::wire::kOpenFlagAppend:
      return "APPEND";
    case fuchsia_io::wire::kOpenFlagNoRemote:
      return "NO_REMOTE";
    case fuchsia_io::wire::kOpenFlagNodeReference:
      return "NODE_REFEREFENCE";
    case fuchsia_io::wire::kOpenFlagDescribe:
      return "DESCRIBE";
    case fuchsia_io::wire::kOpenFlagPosixDeprecated:
      return "POSIX_DEPRECATED";
    case fuchsia_io::wire::kOpenFlagPosixWritable:
      return "POSIX_WRITABLE";
    case fuchsia_io::wire::kOpenFlagPosixExecutable:
      return "POSIX_EXECUTABLE";
    case fuchsia_io::wire::kOpenFlagNotDirectory:
      return "NOT_DIRECTORY";
    case fuchsia_io::wire::kCloneFlagSameRights:
      return "CLONE_SAME_RIGHTS";
    default:
      return "(Unknown flag)";
  }
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, ZxFlags flags) {
  bool first = true;
  uint32_t bit = 1;
  for (int i = 0; i < 32; i++) {
    const uint32_t flag = flags.value & bit;
    if (flag) {
      const char* desc = FlagToString(flag);
      if (!first) {
        sb->Append(" | ");
      }
      first = false;
      sb->Append(desc);
    }
    bit = bit << 1U;
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
