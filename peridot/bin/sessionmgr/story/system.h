// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_SYSTEM_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_SYSTEM_H_

#include <src/lib/fxl/macros.h>

namespace modular {

// Common interface for all story runtime systems.
class System {
 public:
  System() {}
  virtual ~System();

  // TODO(thatguy): Add lifecycle methods Initialize() and Teardown().
  // TODO(thatguy): Add Inspect API hooks for debug output.

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_SYSTEM_H_
