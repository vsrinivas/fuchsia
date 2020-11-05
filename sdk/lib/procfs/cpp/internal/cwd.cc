// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/procfs/cpp/internal/cwd.h>
#include <lib/vfs/cpp/internal/directory.h>

#include <utility>

namespace procfs {
namespace internal {

class CwdDir final : public vfs::internal::Directory {
 public:
  ~CwdDir() override = default;

 protected:
  // |Node| implementation
  zx_status_t Connect(uint32_t flags, zx::channel request, async_dispatcher_t* dispatcher) final {
    // This implementation is slightly inefficient because we end up making an extra connection to
    // the CWD. First, fdio_cwd_clone gives us a zx::channel with the same flags as the existing
    // connection. Second, we call |Clone| to pass the requested |flags|.
    //
    // We could make this operation more efficient if we added an "async" variant of fdio_cwd_clone
    // that let us pass in the desired |flags| and the request handle. However, that would require
    // plumbing that variant through ZXIO as well, which seems unnecessarily complex for this use
    // case.
    //
    // We do not want to cache the |cwd| on this object (e.g., by using vfs::RemoteDir) because
    // the entry in procfs is supposed to update as the cwd for the process changes.
    zx::channel cwd;
    zx_status_t status = fdio_cwd_clone(cwd.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
    if (!cwd.is_valid()) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    fuchsia::io::DirectorySyncPtr cwd_dir;
    cwd_dir.Bind(std::move(cwd));
    return cwd_dir->Clone(flags, fidl::InterfaceRequest<fuchsia::io::Node>(std::move(request)));
  }

  // |Directory| implementation
  zx_status_t Readdir(uint64_t offset, void* data, uint64_t len, uint64_t* out_offset,
                      uint64_t* out_actual) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  vfs::NodeKind::Type GetKind() const final { return Directory::GetKind() | vfs::NodeKind::kRemote; }
};

std::unique_ptr<vfs::internal::Node> CreateCwd() {
  return std::make_unique<CwdDir>();
}

}  // namespace internal
}  // namespace procfs
