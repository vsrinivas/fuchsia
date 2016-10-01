// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

ssize_t cmdline_get(const char* cmdline, const char* key, char* value, size_t n);
uint32_t cmdline_get_uint32(const char* cmdline, const char* key, uint32_t _default);
