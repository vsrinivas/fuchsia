// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdio>

#define PAVER_PREFIX "paver:"
#define ERROR(fmt, ...) fprintf(stderr, PAVER_PREFIX "[%s] " fmt, __FUNCTION__, ##__VA_ARGS__);
#define LOG(fmt, ...) fprintf(stderr, PAVER_PREFIX "[%s] " fmt, __FUNCTION__, ##__VA_ARGS__);

namespace paver {

// Warn users about issues in a way that is intended to stand out from
// typical error logs. These errors typically require user intervention,
// or may result in data loss.
[[maybe_unused]] static void Warn(const char* problem, const char* action) {
  ERROR("-----------------------------------------------------\n");
  ERROR("\n");
  ERROR("%s:\n", problem);
  ERROR("%s\n", action);
  ERROR("\n");
  ERROR("-----------------------------------------------------\n");
}

}  // namespace paver
