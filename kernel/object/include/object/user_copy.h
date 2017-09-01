// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stddef.h>

#include <magenta/types.h>
#include <mxtl/string_piece.h>

mx_status_t magenta_copy_from_user(const void* src, void* dest, size_t len);

// magenta_copy_user_string will copy src_len bytes from src to buf, and will append a NULL.
// If src_len == buf_len, the last character will be replaced with a NULL (see MG-1025).
mx_status_t magenta_copy_user_string(const char* src, size_t src_len, char* buf, size_t buf_len,
                                     mxtl::StringPiece* sp);
