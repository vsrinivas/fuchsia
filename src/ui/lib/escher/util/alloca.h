// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_ALLOCA_H_
#define SRC_UI_LIB_ESCHER_UTIL_ALLOCA_H_

#include <alloca.h>

#include "src/ui/lib/escher/util/align.h"

// Type-safe wrapper around alloca().
#define ESCHER_ALLOCA(T, COUNT)                   \
  escher::NextAlignedTriviallyDestructiblePtr<T>( \
      alloca(((COUNT) + 1) * sizeof(T)));

#endif  // SRC_UI_LIB_ESCHER_UTIL_ALLOCA_H_
