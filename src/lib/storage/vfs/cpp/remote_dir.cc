// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/remote_dir.h"

#include <fidl/fuchsia.io/cpp/wire.h>

#include <utility>

#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace fio = fuchsia_io;

namespace fs {

RemoteDir::RemoteDir(fidl::ClientEnd<fio::Directory> remote_dir_client)
    : remote_client_(std::move(remote_dir_client)) {
  ZX_DEBUG_ASSERT(remote_client_);
}

RemoteDir::~RemoteDir() = default;

VnodeProtocolSet RemoteDir::GetProtocols() const { return VnodeProtocol::kDirectory; }

zx_status_t RemoteDir::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->inode = fio::wire::kInoUnknown;
  attr->link_count = 1;
  return ZX_OK;
}

bool RemoteDir::IsRemote() const { return true; }

zx_status_t RemoteDir::OpenRemote(fio::OpenFlags flags, uint32_t mode, fidl::StringView path,
                                  fidl::ServerEnd<fio::Node> object) const {
  return fidl::WireCall(remote_client_)->Open(flags, mode, path, std::move(object)).status();
}

zx_status_t RemoteDir::GetNodeInfoForProtocol([[maybe_unused]] VnodeProtocol protocol,
                                              [[maybe_unused]] Rights rights,
                                              VnodeRepresentation* info) {
  *info = VnodeRepresentation::Directory();
  return ZX_OK;
}

}  // namespace fs
