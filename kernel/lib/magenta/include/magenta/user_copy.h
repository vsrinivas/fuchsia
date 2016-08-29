// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <mxtl/string_piece.h>

status_t magenta_copy_from_user(const void* src, void* dest, size_t len);

status_t magenta_copy_user_string(const char* src, size_t src_len, char* buf, size_t buf_len,
                                  mxtl::StringPiece* sp);

status_t magenta_copy_user_dynamic(const void* src, void** dst, size_t len, size_t max_len);
