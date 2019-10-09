// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs_types.h>

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/vfs.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

VnodeConnectionOptions VnodeConnectionOptions::FromIoV1Flags(uint32_t fidl_flags) {
  VnodeConnectionOptions options;

  // Flags:
  if (fidl_flags & fio::OPEN_FLAG_CREATE) {
    options.flags.create = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_CREATE_IF_ABSENT) {
    options.flags.fail_if_exists = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_TRUNCATE) {
    options.flags.truncate = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_DIRECTORY) {
    options.flags.directory = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_APPEND) {
    options.flags.append = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_NO_REMOTE) {
    options.flags.no_remote = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_NODE_REFERENCE) {
    options.flags.node_reference = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_DESCRIBE) {
    options.flags.describe = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_POSIX) {
    options.flags.posix = true;
  }
  if (fidl_flags & fio::OPEN_FLAG_NOT_DIRECTORY) {
    options.flags.not_directory = true;
  }
  if (fidl_flags & fio::CLONE_FLAG_SAME_RIGHTS) {
    options.flags.clone_same_rights = true;
  }

  // Rights (these are smushed into |fidl_flags| in fuchsia.io v1):
  if (fidl_flags & fio::OPEN_RIGHT_READABLE) {
    options.rights.read = true;
  }
  if (fidl_flags & fio::OPEN_RIGHT_WRITABLE) {
    options.rights.write = true;
  }
  if (fidl_flags & fio::OPEN_RIGHT_ADMIN) {
    options.rights.admin = true;
  }
  if (fidl_flags & fio::OPEN_RIGHT_EXECUTABLE) {
    options.rights.execute = true;
  }

  return options;
}

uint32_t VnodeConnectionOptions::ToIoV1Flags() const {
  uint32_t fidl_flags = 0;

  // Flags:
  if (flags.create) {
    fidl_flags |= fio::OPEN_FLAG_CREATE;
  }
  if (flags.fail_if_exists) {
    fidl_flags |= fio::OPEN_FLAG_CREATE_IF_ABSENT;
  }
  if (flags.truncate) {
    fidl_flags |= fio::OPEN_FLAG_TRUNCATE;
  }
  if (flags.directory) {
    fidl_flags |= fio::OPEN_FLAG_DIRECTORY;
  }
  if (flags.append) {
    fidl_flags |= fio::OPEN_FLAG_APPEND;
  }
  if (flags.no_remote) {
    fidl_flags |= fio::OPEN_FLAG_NO_REMOTE;
  }
  if (flags.node_reference) {
    fidl_flags |= fio::OPEN_FLAG_NODE_REFERENCE;
  }
  if (flags.describe) {
    fidl_flags |= fio::OPEN_FLAG_DESCRIBE;
  }
  if (flags.posix) {
    fidl_flags |= fio::OPEN_FLAG_POSIX;
  }
  if (flags.not_directory) {
    fidl_flags |= fio::OPEN_FLAG_NOT_DIRECTORY;
  }
  if (flags.clone_same_rights) {
    fidl_flags |= fio::CLONE_FLAG_SAME_RIGHTS;
  }

  // Rights (these are smushed into |fidl_flags| in fuchsia.io v1):
  if (rights.read) {
    fidl_flags |= fio::OPEN_RIGHT_READABLE;
  }
  if (rights.write) {
    fidl_flags |= fio::OPEN_RIGHT_WRITABLE;
  }
  if (rights.admin) {
    fidl_flags |= fio::OPEN_RIGHT_ADMIN;
  }
  if (rights.execute) {
    fidl_flags |= fio::OPEN_RIGHT_EXECUTABLE;
  }

  return fidl_flags;
}

VnodeConnectionOptions VnodeConnectionOptions::FilterForNewConnection(
    VnodeConnectionOptions options) {
  VnodeConnectionOptions result;
  result.flags.append = options.flags.append;
  result.flags.node_reference = options.flags.node_reference;
  result.rights = options.rights;
  return result;
}

}  // namespace fs
