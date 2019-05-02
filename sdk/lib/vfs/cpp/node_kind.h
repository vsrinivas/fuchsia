// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_CPP_NODE_KIND_H_
#define LIB_VFS_CPP_NODE_KIND_H_

#include <cstdint>

namespace vfs {

class NodeKind {
 public:
  using Type = uint64_t;

  NodeKind() = delete;

  // Node is a Directory
  static constexpr Type kDirectory = 0x01;

  // Remote node acts as a proxy between client and a remote directory channel.
  static constexpr Type kRemote = 0x02;

  // Node is a File.
  static constexpr Type kFile = 0x04;

  // Node is a service.
  static constexpr Type kService = 0x08;

  // Node is a VMO.
  static constexpr Type kVmo = 0x10;

  // Node is Writable.
  static constexpr Type kWritable = 0x0100;

  // Node is Readable.
  static constexpr Type kReadable = 0x0200;

  // Node can be mounted,
  static constexpr Type kMountable = 0x0400;

  // Node can be truncated on open.
  static constexpr Type kCanTruncate = 0x0800;

  // Node can be created on open.
  static constexpr Type kCreatable = 0x1000;
  static constexpr Type kAppendable = 0x2000;

  static bool IsDirectory(Type kind) {
    return (kind & kDirectory) == kDirectory;
  }

  static bool IsFile(Type kind) { return (kind & kFile) == kFile; }

  static bool IsService(Type kind) { return (kind & kService) == kService; }

  static bool IsVMO(Type kind) { return (kind & kVmo) == kVmo; }

  static bool IsRemote(Type kind) { return (kind & kRemote) == kRemote; }
};

}  // namespace vfs

#endif  // LIB_VFS_CPP_NODE_KIND_H_
