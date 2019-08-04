// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DLFCN_H_
#define SYSROOT_ZIRCON_DLFCN_H_

#include <dlfcn.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Loads a dynamic shared object stored in |vmo|.
// Acts identically to dlopen, but acts on a vmo
// instead of a file path.
//
// Does not take ownership of the input vmo.
void* dlopen_vmo(zx_handle_t vmo, int mode);

// Replace the handle to the "loader service" used to map names
// to VM objects for dlopen et al.  This takes ownership of the
// given handle, and gives the caller ownership of the old handle
// in the return value.
zx_handle_t dl_set_loader_service(zx_handle_t new_svc);

// Ask the active "loader service" (if there is one), to return
// a new connection.  Not all loader services need support this.
// On success, a channel handle to the new connection is returned
// via out.
zx_status_t dl_clone_loader_service(zx_handle_t* out);

__END_CDECLS

#endif  // SYSROOT_ZIRCON_DLFCN_H_
