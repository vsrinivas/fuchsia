// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

class Breakpoint;

class BreakpointObserver {
 public:
  virtual void OnBreakpointHit(Breakpoint* breakpoint) {}
};

}  // namespace zxdb
