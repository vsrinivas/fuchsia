// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>
#include <sys/types.h>

char *
strncpy(char *dest, char const *src, size_t count)
{
    char *tmp = dest;

    while (count-- && (*dest++ = *src++) != '\0')
        ;

    return tmp;
}

