// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_GET_ID_H_
#define LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_GET_ID_H_

#include <lib/sequence/id.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// Gets the sequence identifier associated with the current thread.
//
// If it returns zero, the current thread may not be currently managed by an
// asynchronous runtime, or that the asynchronous runtime does not support a
// notion of sequences.
sequence_id_t sequence_id_get(void);

__END_CDECLS

#endif  // LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_GET_ID_H_
