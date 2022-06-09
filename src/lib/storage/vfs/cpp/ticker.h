// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a helper for gathering metrics timing info.

#ifndef SRC_LIB_STORAGE_VFS_CPP_TICKER_H_
#define SRC_LIB_STORAGE_VFS_CPP_TICKER_H_

#ifdef __Fuchsia__
#include <lib/zx/time.h>
#endif

// Compile-time option to enable metrics collection globally. On by default.
#define ENABLE_METRICS

#if defined(__Fuchsia__) && defined(ENABLE_METRICS)
#define FS_WITH_METRICS
#endif

namespace fs {

#ifdef FS_WITH_METRICS

// Helper class for getting the duration of events.
using Duration = zx::ticks;

class Ticker {
 public:
  explicit Ticker() : ticks_(zx::ticks::now()) {}

  // Returns the time since the Ticker was constructed.
  Duration End() const { return zx::ticks::now() - ticks_; }

 private:
  zx::ticks ticks_;
};

#else

// Null implementation for host-side code.
class Duration {};

class Ticker {
 public:
  Ticker(bool) {}
  Duration End() const { return Duration(); }
};

#endif

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_TICKER_H_
