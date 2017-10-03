// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "lib/suggestion/fidl/user_input.fidl.h"

namespace maxwell {

class AskDispatcher {
 public:
  AskDispatcher() {}
  virtual void DispatchAsk(UserInputPtr input) = 0;
  virtual void BeginSpeechCapture() = 0;
};

}  // namespace maxwell
