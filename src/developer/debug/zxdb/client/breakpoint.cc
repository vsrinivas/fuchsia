// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/breakpoint.h"

namespace zxdb {

Breakpoint::Breakpoint(Session* session)
    : ClientObject(session), weak_factory_(this) {}
Breakpoint::~Breakpoint() {}

void Breakpoint::AddObserver(BreakpointObserver* observer) {
  observers_.AddObserver(observer);
}

void Breakpoint::RemoveObserver(BreakpointObserver* observer) {
  observers_.RemoveObserver(observer);
}

fxl::WeakPtr<Breakpoint> Breakpoint::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace zxdb
