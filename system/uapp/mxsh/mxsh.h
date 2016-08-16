// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

typedef struct builtin {
    const char* name;
    int (*func)(int argc, char** argv);
    const char* desc;
} builtin_t;

extern builtin_t builtins[];