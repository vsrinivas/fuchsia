// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

// These symbols are intended to be defined by the language runtime (eg. libc)

extern mx_handle_t __magenta_process_self;
extern mx_handle_t __magenta_vmar_root_self;

__END_CDECLS
