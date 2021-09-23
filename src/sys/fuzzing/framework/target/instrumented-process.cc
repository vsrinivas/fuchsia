// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

#include "process.h"

namespace fuzzing {

// This class extends |Process| by automatically connecting in a public default constructor. The
// class is instantiated as a singleton below, and lives as long as the process. All other
// fuzzing-related code executed in the target runs as result of the singleton's constructor.
class InstrumentedProcess : public Process {
 public:
  InstrumentedProcess() {
    Process::InstallHooks();
    auto svc = sys::ServiceDirectory::CreateFromNamespace();
    ProcessProxySyncPtr proxy;
    svc->Connect(proxy.NewRequest());
    Process::Connect(std::move(proxy));
  }

  ~InstrumentedProcess() override { FX_NOTREACHED(); }
};

namespace {

// The weakly linked symbols should be examined as late as possible, in order to guarantee all of
// the module constructors execute first. To achieve this, the singleton's constructor uses a
// priority attribute to ensure it is run just before |main|.
[[gnu::init_priority(0xffff)]] InstrumentedProcess gInstrumented;

}  // namespace
}  // namespace fuzzing
