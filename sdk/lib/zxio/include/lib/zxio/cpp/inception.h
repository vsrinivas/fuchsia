// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/zx/handle.h>
#include <lib/zxio/types.h>

// This header exposes some guts of zxio in order to transition fdio to build on
// top of zxio.

// Creates a new zxio_t object wrapping |handle| into storage provided by the specified
// allocation function |allocator|.
//
// On success, returns ZX_OK and initializes a zxio_t instance into the storage provided by the
// allocator. This also stores the context provided by the allocator into |*out_context|.
//
// If |allocator| returns an error or fails to allocate storage, returns
// ZX_ERR_NO_MEMORY and consumes |handle|. The allocator's error value is not
// preserved. The allocator may store additional context into |*out_context| on
// errors if needed.
//
// See zxio_create() for other error values and postconditions.
zx_status_t zxio_create_with_allocator(zx::handle handle, zxio_storage_alloc allocator,
                                       void** out_context);

// Like zxio_create_with_allocator but the caller supplies information about
// |channel| provided by the server through a Describe call or OnOpen event.
//
// Always consumes |node|. May mutate |info| on success.
zx_status_t zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node> node,
                                       fuchsia_io::wire::NodeInfoDeprecated& info,
                                       zxio_storage_alloc allocator, void** out_context);

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_
