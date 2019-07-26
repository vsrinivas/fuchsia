// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_TESTING_TEST_SUBLOOP_H_
#define LIB_ASYNC_TESTING_TEST_SUBLOOP_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// An FFI-friendly generic interface for test loops.
//
// Implementations of this interface may be thread-unsafe and
// non-reentrant. Clients of an async_test_subloop_t* must ensure that the
// operations are only called with a pointer to the subloop that provides them,
// and that no operation is called after a call to finalize.
typedef struct async_test_subloop async_test_subloop_t;

typedef struct async_test_subloop_ops {
  // Sets the fake time. This will always been called with increasing time,
  // and will be called at least once prior to calling any other function.
  void (*advance_time_to)(async_test_subloop_t*, zx_time_t);
  // Dispatches the next due action. Returns non-zero iff a message was
  // dispatched. Calling this may change the default async dispatcher; the
  // caller is responsible for restoring it to its original value.
  uint8_t (*dispatch_next_due_message)(async_test_subloop_t*);
  // Returns what |dispatch_next_due_message| would return but does not
  // perform any work.
  uint8_t (*has_pending_work)(async_test_subloop_t*);
  // Returns the next time at which this loop should be woken up if nothing
  // else happens, or ZX_TIME_INFINITE.
  zx_time_t (*get_next_task_due_time)(async_test_subloop_t*);
  // Destroys the state associated with this loop provider.
  void (*finalize)(async_test_subloop_t*);
} async_test_subloop_ops_t;

struct async_test_subloop {
  const async_test_subloop_ops_t* ops;
};

__END_CDECLS

#endif  // LIB_ASYNC_TESTING_TEST_SUBLOOP_H_
