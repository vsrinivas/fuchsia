// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/process.h"

namespace zxdb {

Process::Process(Session* session, StartType start_type)
    : ClientObject(session), start_type_(start_type), weak_factory_(this) {}
Process::~Process() = default;

void Process::AddObserver(ProcessObserver* observer) { observers_.AddObserver(observer); }

void Process::RemoveObserver(ProcessObserver* observer) { observers_.RemoveObserver(observer); }

fxl::WeakPtr<Process> Process::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

const char* Process::StartTypeToString(Process::StartType start_type) {
  switch (start_type) {
    case StartType::kAttach:
      return "Attach";
    case StartType::kComponent:
      return "Component";
    case StartType::kLaunch:
      return "Launch";
  }
  FXL_NOTREACHED();
  return "";
}

}  // namespace zxdb
