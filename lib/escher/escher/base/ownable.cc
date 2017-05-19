// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/base/ownable.h"

#include "ftl/logging.h"

namespace escher {

int Ownable::FortyTwo() const {
  FTL_LOG(INFO) << "The answer is 42.";
  return 42;
}

}  // namespace escher
