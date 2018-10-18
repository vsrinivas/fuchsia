// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/system.h"

#include "garnet/bin/zxdb/client/setting_schema.h"

namespace zxdb {

System::System(Session* session)
    : ClientObject(session), settings_(GetSchema(), nullptr) {}
System::~System() = default;

void System::AddObserver(SystemObserver* observer) {
  observers_.AddObserver(observer);
}

void System::RemoveObserver(SystemObserver* observer) {
  observers_.RemoveObserver(observer);
}

fxl::RefPtr<SettingSchema> System::GetSchema() {
  // TODO(donosoc): Fill in the target schema.
  static auto schema = fxl::MakeRefCounted<SettingSchema>();
  return schema;
}

}  // namespace zxdb
