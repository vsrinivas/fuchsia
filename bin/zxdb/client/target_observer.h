// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/target.h"

namespace zxdb {

class TargetObserver {
 public:
  // This callback will be called immediately after each state change, so
  // target->state() will represent the new state. In the case of launching,
  // the general callback will be called after the Launch-specific one.
  virtual void DidChangeTargetState(Target* target, Target::State old_state) {}
};

}  // namespace zxdb
