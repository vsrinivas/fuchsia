// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zxio/zxio.h>

#include "local-vnode.h"

namespace fdio_internal {
struct DirentIteratorState;
}

// A local filesystem consisting of LocalVnodes, mapping string names
// to remote handles.
//
// This class is thread-safe.
struct fdio_namespace : public fbl::RefCounted<fdio_namespace> {
 public:
  using LocalVnode = fdio_internal::LocalVnode;
  using DirentIteratorState = fdio_internal::DirentIteratorState;

  DISALLOW_COPY_ASSIGN_AND_MOVE(fdio_namespace);

  static fbl::RefPtr<fdio_namespace> Create() { return fbl::AdoptRef(new fdio_namespace()); }
  ~fdio_namespace();

  // Create a new |fdio_t| object referring to the root of this namespace.
  //
  // Returns |nullptr| on failure.
  fdio_t* OpenRoot() const;

  // Export all remote references and their paths in a flat format.
  zx_status_t Export(fdio_flat_namespace_t** out) const;

  // Reads a single entry from the list of directory entries into a flattened buffer.
  // |state| contains the position of the iteration.
  // |buffer| and |length| describe the buffer for placing the directory entry.
  // If we have reached the end, |out_entry| is set to NULL, and ZX_OK is returned.
  zx_status_t Readdir(const LocalVnode& vn, DirentIteratorState* state, void* buffer, size_t length,
                      zxio_dirent_t** out_entry) const;

  // Create a new |fdio_t| object referring to the object at |path|.
  //
  // This object may represent either a local node, or a remote object.
  zx_status_t Open(fbl::RefPtr<const LocalVnode> vn, const char* path, uint32_t flags,
                   uint32_t mode, fdio_t** out) const;

  // Connect to a remote object within the namespace.
  //
  // Returns an error if |path| does not exist.
  // Returns an error if |path| references a non-remote object.
  zx_status_t Connect(const char* path, uint32_t flags, zx::channel channel) const;

  // Attaches |remote| to |path| within the current namespace.
  zx_status_t Bind(const char* path, zx::channel remote);

  // Detaches a remote object from |path| within the current namespace.
  //
  // Returns ZX_ERR_NOT_FOUND if |path| does not correspond with a bound remote.
  // Returns ZX_ERR_NOT_SUPPORTED if |path| is the root of the namespace.
  // Returns ZX_ERR_INVALID_ARGS for an unsupported |path|.
  zx_status_t Unbind(const char* path);

 private:
  fdio_namespace();

  // Creates a local |fdio_t| object with a connection to a vnode.
  // This object will increase the number of references to the namespace by
  // one.
  fdio_t* CreateConnection(fbl::RefPtr<const LocalVnode> vn) const;

  // Lookup repeatedly to traverse vnodes within the local filesystem.
  //
  // |vn| and |path| are input and output parameters.
  zx_status_t WalkLocked(fbl::RefPtr<const LocalVnode>* vn, const char** path) const
      __TA_REQUIRES(lock_);

  mutable fbl::Mutex lock_;
  fbl::RefPtr<LocalVnode> root_ __TA_GUARDED(lock_);
};
