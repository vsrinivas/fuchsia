// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_RUNTIME_FLAG_H_
#define LIB_FIDL_RUNTIME_FLAG_H_

#include <stdbool.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Configure if the C/LLCPP bindings should write static unions as extensible unions for
// all outgoing traffic in this process. This function is thread-safe.
void fidl_global_set_should_write_union_as_xunion(bool enabled);

// Query if the C/LLCPP bindings should write static unions as extensible unions for
// all outgoing traffic in this process. This function is thread-safe.
bool fidl_global_get_should_write_union_as_xunion(void);

__END_CDECLS

#endif  // LIB_FIDL_RUNTIME_FLAG_H_
