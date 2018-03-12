// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

class Target;

class SystemObserver {
 public:
  // Called immediately after creation / before destruction of a target.
  virtual void DidCreateTarget(Target* target) {}
  virtual void WillDestroyTarget(Target* target) {}
};

}  // namespace zxdb
