// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>
#include <sys/types.h>

char *
strchr(const char *s, int c)
{
    for (; *s != (char) c; ++s)
        if (*s == '\0')
            return NULL;
    return (char *) s;
}
