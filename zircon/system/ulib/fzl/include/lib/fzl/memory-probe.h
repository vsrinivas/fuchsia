// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

__BEGIN_CDECLS;

// Returns true if the byte at the given address can be read or written.
//
// These functions are designed for testing purposes. They are very heavyweight since they spin up a
// thread to attempt the memory access.
//
// If there is a failure creating a thread, the return value will be false.
//
// The write probe is implemented as a non-atomic read/write of the same value. If the address could
// be modified by a different thread, or if it falls in the region of the stack used by the
// implementation of probe_for_write itself (since the probe is asynchronous), the non-atomic
// read/write can corrupt the data.
bool probe_for_read(const void* addr);
bool probe_for_write(void* addr);

__END_CDECLS;
