// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

void fail(const char* message);
void *memcpy(void *dest, const void *src, size_t count);
int strcmp(char const *cs, char const *ct);
int strncmp(char const *cs, char const *ct, size_t count);

