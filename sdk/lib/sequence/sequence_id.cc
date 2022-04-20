// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sequence/get_id.h>
#include <lib/sequence/set_id.h>
#include <threads.h>
#include <zircon/compiler.h>

namespace {

thread_local sequence_id_t current_sequence_id = 0;

}  // namespace

__EXPORT
sequence_id_t sequence_id_get() { return current_sequence_id; }

__EXPORT
void sequence_id_set(sequence_id_t id) { current_sequence_id = id; }
