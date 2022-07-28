// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_FDIO_STATE_H_
#define LIB_FDIO_FDIO_STATE_H_

#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <sys/types.h>  // mode_t
#include <threads.h>    // mtx_t

#include <array>

#include "sdk/lib/fdio/cleanpath.h"
#include "sdk/lib/fdio/fdio_slot.h"

struct fdio_state_t {
  mtx_t lock;
  mtx_t cwd_lock __TA_ACQUIRED_BEFORE(lock);
  mode_t umask __TA_GUARDED(lock);
  fdio_slot root __TA_GUARDED(lock);
  fdio_slot cwd __TA_GUARDED(lock);
  std::array<fdio_slot, FDIO_MAX_FD> fdtab __TA_GUARDED(lock);
  fdio_ns_t* ns __TA_GUARDED(lock);
  fdio_internal::PathBuffer cwd_path __TA_GUARDED(cwd_lock);
};

extern fdio_state_t __fdio_global_state;

#define fdio_lock (__fdio_global_state.lock)
#define fdio_root_handle (__fdio_global_state.root)
#define fdio_cwd_handle (__fdio_global_state.cwd)
#define fdio_cwd_lock (__fdio_global_state.cwd_lock)
#define fdio_cwd_path (__fdio_global_state.cwd_path)
#define fdio_fdtab (__fdio_global_state.fdtab)
#define fdio_root_ns (__fdio_global_state.ns)

#endif  // LIB_FDIO_FDIO_STATE_H_
