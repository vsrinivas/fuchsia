// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process.h"

#include "garnet/bin/zxdb/client/setting_schema.h"

namespace zxdb {

Process::Process(Session* session)
    : ClientObject(session),
      // Implementations should insert a fallback if needed.
      settings_(GetSchema(), nullptr),
      weak_factory_(this) {}
Process::~Process() = default;

void Process::AddObserver(ProcessObserver* observer) {
  observers_.AddObserver(observer);
}

void Process::RemoveObserver(ProcessObserver* observer) {
  observers_.RemoveObserver(observer);
}

fxl::WeakPtr<Process> Process::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

fxl::RefPtr<SettingSchema> Process::GetSchema() {
  static auto schema = fxl::MakeRefCounted<SettingSchema>();
  return schema;
}

}  // namespace zxdb
