// Copyright 2016 The Fuchsia Authors
// Copyright 2001, Manuel J. Petit
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>
#include <sys/types.h>

char *
strrchr(char const *s, int c)
{
    char const *last= c?0:s;


    while (*s) {
        if (*s== c) {
            last= s;
        }

        s+= 1;
    }

    return (char *)last;
}
