// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/target.h"

namespace zxdb {

Target::Target(Session* session) : ClientObject(session), weak_factory_(this) {}
Target::~Target() = default;

void Target::AddObserver(TargetObserver* observer) {
  observers_.AddObserver(observer);
}

void Target::RemoveObserver(TargetObserver* observer) {
  observers_.RemoveObserver(observer);
}

fxl::WeakPtr<Target> Target::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace zxdb
