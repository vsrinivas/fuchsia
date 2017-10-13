// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace escher {

// A simple interface that can be used without depending on the actual concrete
// type that implements it.
class Timestamper {
 public:
  virtual void AddTimestamp(const char* name) = 0;
};

}  // namespace escher
