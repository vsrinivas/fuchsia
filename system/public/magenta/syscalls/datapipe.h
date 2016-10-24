// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Macros for mx_datapipe_*().

#pragma once

#include <magenta/syscalls/types.h>

// mx_datapipe_write() and mx_datapipe_begin_write():
#define MX_DATAPIPE_WRITE_FLAG_ALL_OR_NONE  1u
// Mask for all the valid MX_DATAPIPE_WRITE_FLAG_... flags:
#define MX_DATAPIPE_WRITE_FLAG_MASK         1u

// mx_datapipe_read() and mx_datapipe_begin_read():
// DISCARD, QUERY, and PEEK are mutually exclusive.
#define MX_DATAPIPE_READ_FLAG_ALL_OR_NONE   1u
#define MX_DATAPIPE_READ_FLAG_DISCARD       2u
#define MX_DATAPIPE_READ_FLAG_QUERY         4u
#define MX_DATAPIPE_READ_FLAG_PEEK          8u
// Mask for all the valid MX_DATAPIPE_READ_FLAG_... flags:
#define MX_DATAPIPE_READ_FLAG_MASK          15u
