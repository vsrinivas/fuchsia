// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

__BEGIN_CDECLS;

#include <inttypes.h>

void backtrace(mx_handle_t, uintptr_t pc, uintptr_t fp);

__END_CDECLS;
