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

  static bool IsReadable(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_RIGHT_READABLE) != fuchsia::io::OpenFlags();
  }

  static bool IsWritable(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_RIGHT_WRITABLE) != fuchsia::io::OpenFlags();
  }

  static bool IsExecutable(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_RIGHT_EXECUTABLE) != fuchsia::io::OpenFlags();
  }

  static bool IsDirectory(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_FLAG_DIRECTORY) != fuchsia::io::OpenFlags();
  }

  static bool IsNotDirectory(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_FLAG_NOT_DIRECTORY) != fuchsia::io::OpenFlags();
  }

  static bool ShouldDescribe(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_FLAG_DESCRIBE) != fuchsia::io::OpenFlags();
  }

  static bool IsNodeReference(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_FLAG_NODE_REFERENCE) != fuchsia::io::OpenFlags();
  }

  static bool ShouldCloneWithSameRights(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::CLONE_FLAG_SAME_RIGHTS) != fuchsia::io::OpenFlags();
  }

  static bool IsPosixWritable(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_FLAG_POSIX_WRITABLE) != fuchsia::io::OpenFlags();
  }

  static bool IsPosixExecutable(fuchsia::io::OpenFlags flags) {
    return (flags & fuchsia::io::OPEN_FLAG_POSIX_EXECUTABLE) != fuchsia::io::OpenFlags();
  }

  // All known rights.
  static constexpr fuchsia::io::OpenFlags kFsRights = fuchsia::io::OPEN_RIGHTS;

  // All lower 16 bits are reserved for future rights extensions.
  static constexpr fuchsia::io::OpenFlags kFsRightsSpace =
      static_cast<fuchsia::io::OpenFlags>(fuchsia::io::OPEN_RIGHTS_MASK);

  // Flags which can be modified by FIDL File::SetFlags.
  static constexpr fuchsia::io::OpenFlags kSettableStatusFlags = fuchsia::io::OPEN_FLAG_APPEND;

  // All flags which indicate state of the connection (excluding rights).
  static constexpr fuchsia::io::OpenFlags kStatusFlags =
      kSettableStatusFlags | fuchsia::io::OPEN_FLAG_NODE_REFERENCE;

  // Returns true if the rights flags in |flags_a| does not exceed
  // those in |flags_b|.
  static bool StricterOrSameRights(fuchsia::io::OpenFlags flags_a, fuchsia::io::OpenFlags flags_b) {
    fuchsia::io::OpenFlags rights_a = flags_a & kFsRights;
    fuchsia::io::OpenFlags rights_b = flags_b & kFsRights;
    return (rights_a & ~rights_b) == fuchsia::io::OpenFlags();
  }

  // Perform basic flags validation relevant to Directory::Open and Node::Clone.
  // Returns false if the flags combination is invalid.
  static bool InputPrecondition(fuchsia::io::OpenFlags flags) {
    // If the caller specified an unknown right, reject the request.
    if (((flags & Flags::kFsRightsSpace) & ~Flags::kFsRights) != fuchsia::io::OpenFlags()) {
      return false;
    }

    // Reject if OPEN_FLAG_DIRECTORY and OPEN_FLAG_NOT_DIRECTORY are both specified.
    if (Flags::IsDirectory(flags) && Flags::IsNotDirectory(flags)) {
      return false;
    }

    // Explicitly reject NODE_REFERENCE together with any invalid flags.
    if (Flags::IsNodeReference(flags)) {
      if ((flags & ~fuchsia::io::OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE) !=
          fuchsia::io::OpenFlags()) {
        return false;
      }
    }

    return true;
  }
};

}  // namespace vfs

#endif  // LIB_VFS_CPP_FLAGS_H_
