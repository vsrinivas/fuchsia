// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_SET_ID_H_
#define LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_SET_ID_H_

#include <lib/sequence/id.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// Sets the sequence identifier associated with the current thread.
//
// Asynchronous runtimes should call this function to set the sequence
// identifier before dispatching any user tasks and notifications on a
// particular thread, and set the sequence identifier to |SEQUENCE_ID_NONE|
// after that thread finishes dispatching user tasks and notifications.
void sequence_id_set(sequence_id_t id);

__END_CDECLS

#endif  // LIB_SEQUENCE_INCLUDE_LIB_SEQUENCE_SET_ID_H_
