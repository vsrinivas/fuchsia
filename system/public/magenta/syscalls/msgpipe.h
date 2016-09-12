// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Types/macros for mx_msgpipe_*().

#pragma once

#include <magenta/syscalls/types.h>

#define MX_MSGPIPE_READ_FLAG_MAY_DISCARD    1u
// Mask for all the valid MX_MSGPIPE_READ_FLAG_... flags:
#define MX_MSGPIPE_READ_FLAG_MASK           1u
