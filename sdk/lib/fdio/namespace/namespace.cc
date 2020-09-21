// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <cerrno>

#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "local-connection.h"
#include "local-filesystem.h"
#include "local-vnode.h"

fdio_t* fdio_ns_open_root(fdio_ns_t* ns) { return ns->OpenRoot(); }

__BEGIN_CDECLS

__EXPORT
zx_status_t fdio_ns_connect(fdio_ns_t* ns, const char* path, uint32_t flags,
                            zx_handle_t raw_handle) {
  zx::channel channel(raw_handle);
  return ns->Connect(path, flags, std::move(channel));
}

__EXPORT
zx_status_t fdio_ns_create(fdio_ns_t** out) {
  // Create a ref-counted object, and leak the reference that is returned
  // via the C API.
  //
  // This reference is reclaimed in fdio_ns_destroy.
  fbl::RefPtr<fdio_namespace> ns = fdio_namespace::Create();
  *out = fbl::ExportToRawPtr(&ns);
  return ZX_OK;
}

__EXPORT
zx_status_t fdio_ns_destroy(fdio_ns_t* raw_ns) {
  // This function reclaims a reference which was leaked in fdio_ns_create.
  __UNUSED auto ns = fbl::ImportFromRawPtr<fdio_namespace>(raw_ns);
  return ZX_OK;
}

__EXPORT
zx_status_t fdio_ns_bind(fdio_ns_t* ns, const char* path, zx_handle_t remote_raw) {
  zx::channel remote(remote_raw);
  return ns->Bind(path, std::move(remote));
}

__EXPORT
zx_status_t fdio_ns_unbind(fdio_ns_t* ns, const char* path) { return ns->Unbind(path); }

__EXPORT
zx_status_t fdio_ns_bind_fd(fdio_ns_t* ns, const char* path, int fd) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_fd_clone(fd, &handle);
  if (status != ZX_OK) {
    return status;
  }

  return fdio_ns_bind(ns, path, handle);
}

__EXPORT
int fdio_ns_opendir(fdio_ns_t* ns) {
  fdio_t* io = ns->OpenRoot();
  if (io == nullptr) {
    errno = ENOMEM;
    return -1;
  }
  int fd = fdio_bind_to_fd(io, -1, 0);
  if (fd < 0) {
    fdio_release(io);
    errno = ENOMEM;
  }
  return fd;
}

__EXPORT
zx_status_t fdio_ns_chdir(fdio_ns_t* ns) {
  fdio_t* io = ns->OpenRoot();
  if (io == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  fdio_chdir(io, "/");
  return ZX_OK;
}

__EXPORT
zx_status_t fdio_ns_export(fdio_ns_t* ns, fdio_flat_namespace_t** out) { return ns->Export(out); }

__EXPORT
zx_status_t fdio_ns_export_root(fdio_flat_namespace_t** out) {
  fbl::AutoLock lock(&fdio_lock);
  return fdio_ns_export(fdio_root_ns, out);
}

__EXPORT
void fdio_ns_free_flat_ns(fdio_flat_namespace_t* ns) {
  zx_handle_close_many(ns->handle, ns->count);
  free(ns);
}

__END_CDECLS
