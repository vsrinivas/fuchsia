// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/vfs_types.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/fit/function.h>

namespace fio = fuchsia_io;

namespace fs {

VnodeConnectionOptions VnodeConnectionOptions::FromIoV1Flags(uint32_t fidl_flags) {
  VnodeConnectionOptions options;

  // Flags:
  if (fidl_flags & fio::wire::kOpenFlagCreate) {
    options.flags.create = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagCreateIfAbsent) {
    options.flags.fail_if_exists = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagTruncate) {
    options.flags.truncate = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagDirectory) {
    options.flags.directory = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagAppend) {
    options.flags.append = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagNoRemote) {
    options.flags.no_remote = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagNodeReference) {
    options.flags.node_reference = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagDescribe) {
    options.flags.describe = true;
  }
  // Expand existing POSIX flag into new equivalent flags.
  // TODO(fxbug.dev/40862): Remove this branch when removing OPEN_FLAG_POSIX.
  if (fidl_flags & fio::wire::kOpenFlagPosix) {
    options.flags.posix_write = true;
    options.flags.posix_execute = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagPosixWritable) {
    options.flags.posix_write = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagPosixExecutable) {
    options.flags.posix_execute = true;
  }
  if (fidl_flags & fio::wire::kOpenFlagNotDirectory) {
    options.flags.not_directory = true;
  }
  if (fidl_flags & fio::wire::kCloneFlagSameRights) {
    options.flags.clone_same_rights = true;
  }

  // Rights (these are smushed into |fidl_flags| in fuchsia.io v1):
  if (fidl_flags & fio::wire::kOpenRightReadable) {
    options.rights.read = true;
  }
  if (fidl_flags & fio::wire::kOpenRightWritable) {
    options.rights.write = true;
  }
  if (fidl_flags & fio::wire::kOpenRightAdmin) {
    options.rights.admin = true;
  }
  if (fidl_flags & fio::wire::kOpenRightExecutable) {
    options.rights.execute = true;
  }

  return options;
}

uint32_t VnodeConnectionOptions::ToIoV1Flags() const {
  uint32_t fidl_flags = 0;

  // Flags:
  if (flags.create) {
    fidl_flags |= fio::wire::kOpenFlagCreate;
  }
  if (flags.fail_if_exists) {
    fidl_flags |= fio::wire::kOpenFlagCreateIfAbsent;
  }
  if (flags.truncate) {
    fidl_flags |= fio::wire::kOpenFlagTruncate;
  }
  if (flags.directory) {
    fidl_flags |= fio::wire::kOpenFlagDirectory;
  }
  if (flags.append) {
    fidl_flags |= fio::wire::kOpenFlagAppend;
  }
  if (flags.no_remote) {
    fidl_flags |= fio::wire::kOpenFlagNoRemote;
  }
  if (flags.node_reference) {
    fidl_flags |= fio::wire::kOpenFlagNodeReference;
  }
  if (flags.describe) {
    fidl_flags |= fio::wire::kOpenFlagDescribe;
  }
  if (flags.posix_write) {
    // TODO(fxbug.dev/40862): Replace kOpenFlagPosix with kOpenFlagPosixWritable after out-of-tree
    // SDK users have been updated.
    fidl_flags |= fio::wire::kOpenFlagPosix;
  }
  if (flags.posix_execute) {
    // TODO(fxbug.dev/40862): Replace kOpenFlagPosix with kOpenFlagPosixExecutable after out-of-tree
    // SDK users have been updated.
    fidl_flags |= fio::wire::kOpenFlagPosix;
  }
  if (flags.not_directory) {
    fidl_flags |= fio::wire::kOpenFlagNotDirectory;
  }
  if (flags.clone_same_rights) {
    fidl_flags |= fio::wire::kCloneFlagSameRights;
  }

  // Rights (these are smushed into |fidl_flags| in fuchsia.io v1):
  if (rights.read) {
    fidl_flags |= fio::wire::kOpenRightReadable;
  }
  if (rights.write) {
    fidl_flags |= fio::wire::kOpenRightWritable;
  }
  if (rights.admin) {
    fidl_flags |= fio::wire::kOpenRightAdmin;
  }
  if (rights.execute) {
    fidl_flags |= fio::wire::kOpenRightExecutable;
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

fio::wire::NodeAttributes VnodeAttributes::ToIoV1NodeAttributes() const {
  return fio::wire::NodeAttributes{.mode = mode,
                                   .id = inode,
                                   .content_size = content_size,
                                   .storage_size = storage_size,
                                   .link_count = link_count,
                                   .creation_time = creation_time,
                                   .modification_time = modification_time};
}

void ConvertToIoV1NodeInfo(VnodeRepresentation representation,
                           fit::callback<void(fio::wire::NodeInfo&&)> callback) {
  representation.visit([&](auto&& repr) {
    using T = std::decay_t<decltype(repr)>;
    fio::wire::NodeInfo info;
    if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Connector>) {
      fio::wire::Service service;
      info.set_service(fidl::ObjectView<fio::wire::Service>::FromExternal(&service));
      callback(std::move(info));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::File>) {
      fio::wire::FileObject file = {.event = std::move(repr.observer)};
      info.set_file(fidl::ObjectView<fio::wire::FileObject>::FromExternal(&file));
      callback(std::move(info));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Directory>) {
      fio::wire::DirectoryObject directory;
      info.set_directory(fidl::ObjectView<fio::wire::DirectoryObject>::FromExternal(&directory));
      callback(std::move(info));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Pipe>) {
      fio::wire::Pipe pipe = {.socket = std::move(repr.socket)};
      info.set_pipe(fidl::ObjectView<fio::wire::Pipe>::FromExternal(&pipe));
      callback(std::move(info));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Memory>) {
      fio::wire::Vmofile vmofile = {
          .vmo = std::move(repr.vmo), .offset = repr.offset, .length = repr.length};
      info.set_vmofile(fidl::ObjectView<fio::wire::Vmofile>::FromExternal(&vmofile));
      callback(std::move(info));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Device>) {
      fio::wire::Device device = {.event = std::move(repr.event)};
      info.set_device(fidl::ObjectView<fio::wire::Device>::FromExternal(&device));
      callback(std::move(info));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Tty>) {
      fio::wire::Tty tty = {.event = std::move(repr.event)};
      info.set_tty(fidl::ObjectView<fio::wire::Tty>::FromExternal(&tty));
      callback(std::move(info));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::DatagramSocket>) {
      fio::wire::DatagramSocket datagram_socket = {.event = std::move(repr.event)};
      info.set_datagram_socket(
          fidl::ObjectView<fio::wire::DatagramSocket>::FromExternal(&datagram_socket));
      callback(std::move(info));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::StreamSocket>) {
      fio::wire::StreamSocket stream_socket = {.socket = std::move(repr.socket)};
      info.set_stream_socket(
          fidl::ObjectView<fio::wire::StreamSocket>::FromExternal(&stream_socket));
      callback(std::move(info));
    } else {
      ZX_PANIC("Representation variant is not initialized");
    }
  });
}

}  // namespace fs
