// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread.h"

#include "garnet/bin/zxdb/client/setting_schema.h"

namespace zxdb {

Thread::Thread(Session* session)
    : ClientObject(session),
      // Implementations should insert a fallback if needed.
      settings_(GetSchema(), nullptr),
      weak_factory_(this) {}
Thread::~Thread() = default;

void Thread::AddObserver(ThreadObserver* observer) {
  observers_.AddObserver(observer);
}

void Thread::RemoveObserver(ThreadObserver* observer) {
  observers_.RemoveObserver(observer);
}

fxl::WeakPtr<Thread> Thread::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

fxl::RefPtr<SettingSchema> Thread::GetSchema() {
  static auto schema = fxl::MakeRefCounted<SettingSchema>();
  return schema;
}

}  // namespace zxdb
