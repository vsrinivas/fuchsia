// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>
#include <sys/types.h>

size_t
strxfrm(char *dest, const char *src, size_t n)
{
    size_t len = strlen(src);

    if (n) {
        size_t copy_len = len < n ? len : n - 1;
        memcpy(dest, src, copy_len);
        dest[copy_len] = 0;
    }
    return len;
}

