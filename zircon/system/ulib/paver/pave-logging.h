// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdio>

#define PAVER_PREFIX "paver:"
#define ERROR(fmt, ...) fprintf(stderr, PAVER_PREFIX "[%s] " fmt, __FUNCTION__, ##__VA_ARGS__);
#define LOG(fmt, ...) fprintf(stderr, PAVER_PREFIX "[%s] " fmt, __FUNCTION__, ##__VA_ARGS__);
