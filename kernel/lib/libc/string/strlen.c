// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>
#include <sys/types.h>

size_t
strlen(char const *s)
{
    size_t i;

    i= 0;
    while (s[i]) {
        i+= 1;
    }

    return i;
}
