// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_ALLOCA_H_
#define LIB_ESCHER_UTIL_ALLOCA_H_

#include <alloca.h>

#include "lib/escher/util/align.h"

// Type-safe wrapper around alloca().
#define ESCHER_ALLOCA(T, COUNT)                   \
  escher::NextAlignedTriviallyDestructiblePtr<T>( \
      alloca(((COUNT) + 1) * sizeof(T)));

#endif  // LIB_ESCHER_UTIL_ALLOCA_H_
