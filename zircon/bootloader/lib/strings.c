// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <strings.h>

int strcasecmp(const char* s1, const char* s2) {
    while (1) {
        int diff = tolower(*s1) - tolower(*s2);
        if (diff != 0 || *s1 == '\0') {
            return diff;
        }
        s1++;
        s2++;
    }
}

int strncasecmp(const char* s1, const char* s2, size_t len) {
    while (len-- > 0) {
        int diff = tolower(*s1) - tolower(*s2);
        if (diff != 0 || *s1 == '\0') {
            return diff;
        }
        s1++;
        s2++;
    }
    return 0;
}
