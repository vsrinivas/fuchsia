// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_ID_H_
#define LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_ID_H_

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// An asynchronous runtime-specific sequence identifier, which identifies a set
// of actions with a total ordering of execution: each subsequent action will
// always observe side-effects from previous actions, if the thread(s)
// performing those actions have the same sequence identifier.
//
// For example, a async dispatcher backed by a thread pool may choose to
// implement a sequence by acquiring a sequence-specific lock before running any
// actions from that sequence, ensuring mutual exclusion within each sequence.
typedef uint64_t sequence_id_t;

// The default sequence ID indicating there is no sequence associated with the
// current thread.
#define SEQUENCE_ID_NONE ((sequence_id_t)0)

__END_CDECLS

#endif  // LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_ID_H_
