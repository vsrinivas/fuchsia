// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process.h"

namespace zxdb {

Process::Process(Session* session) : ClientObject(session) {}
Process::~Process() = default;

void Process::AddObserver(ProcessObserver* observer) {
  observers_.AddObserver(observer);
}

void Process::RemoveObserver(ProcessObserver* observer) {
  observers_.RemoveObserver(observer);
}

}  // namespace zxdb
