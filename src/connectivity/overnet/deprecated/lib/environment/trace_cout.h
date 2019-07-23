// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mutex>

#include "src/connectivity/overnet/deprecated/lib/environment/timer.h"
#include "src/connectivity/overnet/deprecated/lib/environment/trace.h"

#pragma once

namespace overnet {

class TraceCout : public TraceRenderer {
 public:
  TraceCout(Timer* timer) : timer_(timer) {}

  void Render(TraceOutput output) override;
  void NoteParentChild(Op, Op) override {}

 private:
  Timer* const timer_;
  static std::mutex mu_;
};

}  // namespace overnet
