// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdlib.h>
#include <string.h>

char *
strdup(const char *str)
{
    size_t len;
    char *copy;

    len = strlen(str) + 1;
    copy = malloc(len);
    if (copy == NULL)
        return NULL;
    memcpy(copy, str, len);
    return copy;
}

