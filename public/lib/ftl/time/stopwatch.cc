// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/time/stopwatch.h"

#include "lib/ftl/time/time.h"

namespace ftl {

void Stopwatch::Start() {
  start_time_ = Now();
}

Duration Stopwatch::Elapsed() {
  return Now() - start_time_;
}

}  // namespace ftl
