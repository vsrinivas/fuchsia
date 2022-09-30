// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_SEQUENCE_ID_H_
#define LIB_ASYNC_SEQUENCE_ID_H_

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// A dispatcher-specific sequence identifier, which identifies a set of actions
// with a total ordering of execution: each subsequent action will always
// observe side-effects from previous actions, if the thread(s) performing those
// actions have the same sequence identifier.
//
// For example, a dispatcher backed by a thread pool may choose to implement
// sequences by acquiring a sequence-specific lock before running any actions
// from that sequence, ensuring mutual exclusion within each sequence.
typedef struct async_sequence_id {
  uint64_t value;
} async_sequence_id_t;

// Gets the dispatcher-specific sequence identifier of the currently executing
// task.
//
// If the execution context of the calling thread is associated with a sequence,
// the dispatcher should populate the sequence identifier representing the
// current sequence. Otherwise, it should return an error code detailed below.
//
// Returns |ZX_OK| if the sequence identifier was successfully obtained.
// Returns |ZX_ERR_INVALID_ARGS| if the dispatcher supports sequences, but the
// calling thread is not executing a task managed by the dispatcher.
// Returns |ZX_ERR_WRONG_TYPE| if the calling thread is executing a task
// managed by the dispatcher, but that task is not part of a sequence.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// |out_error| will not be mutated when the return value is |ZX_OK|. Otherwise,
// it will be set to a NULL-terminated detailed explanation of the error, which
// may suggest corrective actions that are specific to that asynchronous
// runtime. If set, the error string will have static storage duration (for
// example, an implementation may return string literals).
//
// This operation is thread-safe.
zx_status_t async_get_sequence_id(async_dispatcher_t* dispatcher,
                                  async_sequence_id_t* out_sequence_id, const char** out_error);

// Checks that the the dispatcher-specific sequence identifier of the currently
// executing task is equal to |sequence_id|.
//
// If the sequence identifier of the calling thread cannot be successfully
// obtained, it should return an error code detailed below:
//
// - Returns |ZX_ERR_INVALID_ARGS| if the dispatcher supports sequences, but the
//   calling thread is not executing a task managed by the dispatcher.
// - Returns |ZX_ERR_WRONG_TYPE| if the calling thread is executing a task
//   managed by the dispatcher, but that task is not part of a sequence.
// - Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// Otherwise, the dispatcher should check that the sequence identifier
// representing the current sequence equals to |sequence_id|:
//
// - Returns |ZX_OK| if the sequence identifiers are equal.
// - Returns |ZX_ERR_OUT_OF_RANGE| if the sequence identifiers are not equal.
//
// |out_error| will not be mutated when the return value is |ZX_OK|. Otherwise,
// it will be set to a NULL-terminated detailed explanation of the error, which
// may suggest corrective actions that are specific to that asynchronous
// runtime. If set, the error string will have static storage duration (for
// example, an implementation may return string literals).
//
// This operation is thread-safe.
zx_status_t async_check_sequence_id(async_dispatcher_t* dispatcher, async_sequence_id_t sequence_id,
                                    const char** out_error);

__END_CDECLS

#endif  // LIB_ASYNC_SEQUENCE_ID_H_
