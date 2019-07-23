// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/vfs/cpp/remote_dir.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

namespace vfs {

RemoteDir::RemoteDir(zx::channel remote_dir, async_dispatcher_t* dispatcher) {
  ZX_ASSERT(remote_dir.is_valid());
  dir_ptr_.Bind(std::move(remote_dir), dispatcher);
}

RemoteDir::RemoteDir(fidl::InterfaceHandle<fuchsia::io::Directory> dir,
                     async_dispatcher_t* dispatcher) {
  ZX_ASSERT(dir.is_valid());
  dir_ptr_.Bind(std::move(dir), dispatcher);
}

RemoteDir::RemoteDir(fuchsia::io::DirectoryPtr dir_ptr) : dir_ptr_(std::move(dir_ptr)) {
  ZX_ASSERT(dir_ptr_.is_bound());
}

RemoteDir::~RemoteDir() = default;

zx_status_t RemoteDir::Connect(uint32_t flags, zx::channel request,
                               async_dispatcher_t* dispatcher) {
  dir_ptr_->Clone(flags, fidl::InterfaceRequest<fuchsia ::io::Node>(std::move(request)));
  return ZX_OK;
}

zx_status_t RemoteDir::Readdir(uint64_t offset, void* data, uint64_t len, uint64_t* out_offset,
                               uint64_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

NodeKind::Type RemoteDir::GetKind() const { return Directory::GetKind() | NodeKind::kRemote; }

}  // namespace vfs
