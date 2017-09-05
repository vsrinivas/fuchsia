// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>

__BEGIN_CDECLS

// Gets the current thread's default asynchronous dispatcher interface.
// Returns |NULL| if none.
async_t* async_get_default(void);

// Sets the current thread's default asynchronous dispatcher interface.
// May be set to |NULL| if this thread doesn't have a default dispatcher.
void async_set_default(async_t* async);

__END_CDECLS
