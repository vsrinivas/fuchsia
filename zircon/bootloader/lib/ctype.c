// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>

int isdigit(int c) {
    return (c >= '0') && (c <= '9');
}

int isspace(int c) {
    return (c == ' ')  ||
           (c == '\f') ||
           (c == '\n') ||
           (c == '\r') ||
           (c == '\t') ||
           (c == '\v');
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

