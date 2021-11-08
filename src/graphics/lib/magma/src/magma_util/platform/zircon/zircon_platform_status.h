// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_STATUS_H
#define ZIRCON_PLATFORM_STATUS_H

#include <zircon/types.h>

#include "magma_util/macros.h"
#include "magma_util/status.h"

namespace magma {

inline zx_status_t ToZxStatus(magma_status_t status) {
  switch (status) {
    case MAGMA_STATUS_OK:
      return ZX_OK;
    case MAGMA_STATUS_INTERNAL_ERROR:
      return ZX_ERR_INTERNAL;
    case MAGMA_STATUS_INVALID_ARGS:
      return ZX_ERR_INVALID_ARGS;
    case MAGMA_STATUS_ACCESS_DENIED:
      return ZX_ERR_ACCESS_DENIED;
    case MAGMA_STATUS_MEMORY_ERROR:
      return ZX_ERR_NO_MEMORY;
    case MAGMA_STATUS_CONTEXT_KILLED:
      return ZX_ERR_IO;
    case MAGMA_STATUS_CONNECTION_LOST:
      return ZX_ERR_PEER_CLOSED;
    case MAGMA_STATUS_TIMED_OUT:
      return ZX_ERR_TIMED_OUT;
    case MAGMA_STATUS_UNIMPLEMENTED:
      return ZX_ERR_NOT_SUPPORTED;
    case MAGMA_STATUS_BAD_STATE:
      return ZX_ERR_BAD_STATE;
    default:
      DMESSAGE("No match for magma status %d", status);
      DASSERT(false);
      return ZX_ERR_INTERNAL;
  }
}

inline Status FromZxStatus(zx_status_t zx_status) {
  switch (zx_status) {
    case ZX_OK:
      return MAGMA_STATUS_OK;
    case ZX_ERR_INTERNAL:
      return MAGMA_STATUS_INTERNAL_ERROR;
    case ZX_ERR_INVALID_ARGS:
      return MAGMA_STATUS_INVALID_ARGS;
    case ZX_ERR_ACCESS_DENIED:
      return MAGMA_STATUS_ACCESS_DENIED;
    case ZX_ERR_NO_MEMORY:
      return MAGMA_STATUS_MEMORY_ERROR;
    case ZX_ERR_IO:
      return MAGMA_STATUS_CONTEXT_KILLED;
    case ZX_ERR_PEER_CLOSED:
    case ZX_ERR_CANCELED:
      return MAGMA_STATUS_CONNECTION_LOST;
    case ZX_ERR_TIMED_OUT:
      return MAGMA_STATUS_TIMED_OUT;
    case ZX_ERR_NOT_SUPPORTED:
      return MAGMA_STATUS_UNIMPLEMENTED;
    case ZX_ERR_BAD_STATE:
      return MAGMA_STATUS_BAD_STATE;
    default:
      DMESSAGE("No match for zx status %d", zx_status);
      DASSERT(false);
      return MAGMA_STATUS_INTERNAL_ERROR;
  }
}

}  // namespace magma

#endif
