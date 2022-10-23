// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_NAMESPACE_NAMESPACE_H_
#define LIB_FDIO_NAMESPACE_NAMESPACE_H_

#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/result.h>

#include <fbl/ref_ptr.h>

struct fdio;

// Creates an |fdio_t| referencing the root of the |ns| namespace.
zx::result<fbl::RefPtr<fdio>> fdio_ns_open_root(fdio_ns_t* ns);

// Change the root of the given namespace |ns| to match |io|.
//
// Does not take ownership of |io|. The caller is responsible for retaining a reference to |io|
// for the duration of this call and for releasing that reference after this function returns.
zx_status_t fdio_ns_set_root(fdio_ns_t* ns, fdio_t* io);

#endif  // LIB_FDIO_NAMESPACE_NAMESPACE_H_
