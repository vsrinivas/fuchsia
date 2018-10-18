// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/target.h"

#include "garnet/bin/zxdb/client/setting_schema.h"

namespace zxdb {

Target::Target(Session* session)
    : ClientObject(session),
      settings_(GetSchema(), nullptr),
      weak_factory_(this) {
  // TODO(donosoc): Hook up the target -> system fallback.
  //                This should be done in the implementation.
}

Target::~Target() = default;

void Target::AddObserver(TargetObserver* observer) {
  observers_.AddObserver(observer);
}

void Target::RemoveObserver(TargetObserver* observer) {
  observers_.RemoveObserver(observer);
}

fxl::WeakPtr<Target> Target::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

fxl::RefPtr<SettingSchema> Target::GetSchema() {
  // TODO(donosoc): Fill in the target schema.
  static auto schema = fxl::MakeRefCounted<SettingSchema>();
  return schema;
}

}  // namespace zxdb
