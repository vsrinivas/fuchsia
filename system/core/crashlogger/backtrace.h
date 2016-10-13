// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

__BEGIN_CDECLS;

#include <inttypes.h>

void backtrace(mx_handle_t process, mx_handle_t thread,
               uintptr_t pc, uintptr_t sp, uintptr_t fp,
               bool use_libunwind);

__END_CDECLS;
