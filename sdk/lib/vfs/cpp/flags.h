// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_CPP_FLAGS_H_
#define LIB_VFS_CPP_FLAGS_H_

#include <fuchsia/io/cpp/fidl.h>

namespace vfs {

class Flags {
 public:
  Flags() = delete;

  static bool IsReadable(uint32_t flags) {
    return (flags & fuchsia::io::OPEN_RIGHT_READABLE) != 0;
  }

  static bool IsWritable(uint32_t flags) {
    return (flags & fuchsia::io::OPEN_RIGHT_WRITABLE) != 0;
  }

  static bool IsAdminable(uint32_t flags) {
    return (flags & fuchsia::io::OPEN_RIGHT_ADMIN) != 0;
  }

  static bool IsDirectory(uint32_t flags) {
    return (flags & fuchsia::io::OPEN_FLAG_DIRECTORY) != 0;
  }

  static bool IsNotDirectory(uint32_t flags) {
    return (flags & fuchsia::io::OPEN_FLAG_NOT_DIRECTORY) != 0;
  }

  static bool ShouldDescribe(uint32_t flags) {
    return (flags & fuchsia::io::OPEN_FLAG_DESCRIBE) != 0;
  }

  static bool IsNodeReference(uint32_t flags) {
    return (flags & fuchsia::io::OPEN_FLAG_NODE_REFERENCE) != 0;
  }

  static bool ShouldCloneWithSameRights(uint32_t flags) {
    return (flags & fuchsia::io::CLONE_FLAG_SAME_RIGHTS) != 0;
  }

  static bool IsPosix(uint32_t flags) {
    return (flags & fuchsia::io::OPEN_FLAG_POSIX) != 0;
  }

  // All known rights.
  static constexpr uint32_t kFsRights =
      fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE |
      fuchsia::io::OPEN_RIGHT_ADMIN;

  // All lower 16 bits are reserved for future rights extensions.
  static constexpr uint32_t kFsRightsSpace = 0x0000FFFF;

  // Flags which can be modified by FIDL File::SetFlags.
  static constexpr uint32_t kSettableStatusFlags =
      fuchsia::io::OPEN_FLAG_APPEND;

  // All flags which indicate state of the connection (excluding rights).
  static constexpr uint32_t kStatusFlags =
      kSettableStatusFlags | fuchsia::io::OPEN_FLAG_NODE_REFERENCE;

  // Returns true if the rights flags in |flags_a| does not exceed
  // those in |flags_b|.
  static bool StricterOrSameRights(uint32_t flags_a, uint32_t flags_b) {
    uint32_t rights_a = flags_a & kFsRights;
    uint32_t rights_b = flags_b & kFsRights;
    return (rights_a & ~rights_b) == 0;
  }

  // Perform basic flags validation relevant to Directory::Open and Node::Clone.
  // Returns false if the flags combination is invalid.
  static bool InputPrecondition(uint32_t flags) {
    // If the caller specified an unknown right, reject the request.
    if ((flags & Flags::kFsRightsSpace) & ~Flags::kFsRights) {
      return false;
    }

    // Explicitly reject NODE_REFERENCE together with any invalid flags.
    if (Flags::IsNodeReference(flags)) {
      constexpr uint32_t kValidFlagsForNodeRef =
          fuchsia::io::OPEN_FLAG_NODE_REFERENCE |
          fuchsia::io::OPEN_FLAG_DIRECTORY |
          fuchsia::io::OPEN_FLAG_NOT_DIRECTORY |
          fuchsia::io::OPEN_FLAG_DESCRIBE;
      if (flags & ~kValidFlagsForNodeRef) {
        return false;
      }
    }

    return true;
  }
};

}  // namespace vfs

#endif  // LIB_VFS_CPP_FLAGS_H_
