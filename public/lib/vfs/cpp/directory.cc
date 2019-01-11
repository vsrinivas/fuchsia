
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

std::unique_ptr<Connection> Directory::CreateConnection(uint32_t flags) {
  return std::make_unique<internal::DirectoryConnection>(flags, this);
}

}  // namespace vfs
