// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs_types.h>

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/fit/function.h>

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

fio::NodeAttributes VnodeAttributes::ToIoV1NodeAttributes() const {
  return fio::NodeAttributes{
      .mode = mode,
      .id = inode,
      .content_size = content_size,
      .storage_size = storage_size,
      .link_count = link_count,
      .creation_time = creation_time,
      .modification_time = modification_time
  };
}

void ConvertToIoV1NodeInfo(VnodeRepresentation representation,
                           fit::callback<void(fio::NodeInfo*)> callback) {
  representation.visit([&](auto&& repr) {
    using T = std::decay_t<decltype(repr)>;
    fio::NodeInfo info;
    if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Connector>) {
      fio::Service service;
      info.set_service(std::move(service));
      callback(&info);
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::File>) {
      fio::FileObject file = {
          .event = std::move(repr.observer)
      };
      info.set_file(std::move(file));
      callback(&info);
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Directory>) {
      fio::DirectoryObject directory;
      info.set_directory(std::move(directory));
      callback(&info);
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Pipe>) {
      fio::Pipe pipe = {
          .socket = std::move(repr.socket)
      };
      info.set_pipe(std::move(pipe));
      callback(&info);
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Memory>) {
      fio::Vmofile vmofile = {
          .vmo = std::move(repr.vmo),
          .offset = repr.offset,
          .length = repr.length
      };
      info.set_vmofile(std::move(vmofile));
      callback(&info);
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Device>) {
      fio::Device device = {
          .event = std::move(repr.event)
      };
      info.set_device(std::move(device));
      callback(&info);
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Tty>) {
      fio::Tty tty = {
          .event = std::move(repr.event)
      };
      info.set_tty(std::move(tty));
      callback(&info);
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Socket>) {
      fio::Socket socket = {
          .socket = std::move(repr.socket)
      };
      info.set_socket(std::move(socket));
      callback(&info);
    } else {
      ZX_PANIC("Representation variant is not initialized");
    }
  });
}

}  // namespace fs
