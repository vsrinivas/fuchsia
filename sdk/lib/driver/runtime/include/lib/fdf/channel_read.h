// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_READ_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_READ_H_

#include <lib/async/dispatcher.h>
#include <lib/fdf/channel.h>
#include <lib/fdf/dispatcher.h>

__BEGIN_CDECLS

typedef struct fdf_channel_read fdf_channel_read_t;

// Handles execution of asynchronous read operations.
//
// The |status| is |ZX_OK| if the channel is ready to be read.
// The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
// the read handler ran.
typedef void(fdf_channel_read_handler_t)(fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read,
                                         zx_status_t status);

// Holds context for an asynchronous read operation and its handler.
//
// After successfully scheduling the read, the client is responsible for retaining
// the structure in memory (and unmodified) until the read handler runs, or the
// dispatcher shuts down.  Thereafter, the read may be started again or destroyed.
struct fdf_channel_read {
  async_state_t state;
  fdf_channel_read_handler_t* handler;
  fdf_handle_t channel;
  // TODO(fxbug.dev/86294): Decide if we want a sticky option akin to ZX_WAIT_ASYNC_REPEATING
  // (and maybe default to it when the dispatcher is unsynchronized?).
  uint32_t options;
};

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_READ_H_
