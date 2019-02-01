
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/directory.h>

#include <lib/vfs/cpp/internal/directory_connection.h>

namespace vfs {

Directory::Directory() = default;

Directory::~Directory() = default;

void Directory::Describe(fuchsia::io::NodeInfo* out_info) {
  out_info->set_directory(fuchsia::io::DirectoryObject());
}

zx_status_t Directory::Lookup(const std::string& name, Node** out_node) {
  return ZX_ERR_NOT_FOUND;
}

zx_status_t Directory::CreateConnection(
    uint32_t flags, std::unique_ptr<Connection>* connection) {
  *connection = std::make_unique<internal::DirectoryConnection>(flags, this);
  return ZX_OK;
}

uint32_t Directory::GetAdditionalAllowedFlags() const {
  return fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE |
         fuchsia::io::OPEN_FLAG_DIRECTORY;
}

uint32_t Directory::GetProhibitiveFlags() const {
  return fuchsia::io::OPEN_FLAG_CREATE |
         fuchsia::io::OPEN_FLAG_CREATE_IF_ABSENT |
         fuchsia::io::OPEN_FLAG_TRUNCATE | fuchsia::io::OPEN_FLAG_APPEND;
}

bool Directory::IsDirectory() const { return true; }

}  // namespace vfs
