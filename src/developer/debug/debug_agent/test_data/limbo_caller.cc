// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>

#include <src/lib/fxl/logging.h>

// Simple application that obtains the Process Limbo services and obtains the exceptions from it.
// Meant to be called manually for testing purposes.

namespace {

std::string GetProcessName(const fuchsia::exception::ProcessException& pe) {
  if (pe.has_process())
    return fsl::GetObjectName(pe.process().get());

  zx::process process;
  FXL_DCHECK(pe.exception().get_process(&process) == ZX_OK);
  return fsl::GetObjectName(process.get());
}

std::string GetThreadName(const fuchsia::exception::ProcessException& pe) {
  if (pe.has_thread())
    return fsl::GetObjectName(pe.thread().get());

  zx::thread thread;
  FXL_DCHECK(pe.exception().get_thread(&thread) == ZX_OK);
  return fsl::GetObjectName(thread.get());
}

}  // namespace

int main() {
  auto environment_services = sys::ServiceDirectory::CreateFromNamespace();

  fuchsia::exception::ProcessLimboSyncPtr process_limbo;
  environment_services->Connect(process_limbo.NewRequest());

  std::vector<fuchsia::exception::ProcessException> exceptions;
  zx_status_t status = process_limbo->GetProcessesWaitingOnException(&exceptions);
  FXL_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  printf("Got %zu exceptions.\n", exceptions.size());
  fflush(stdout);
  for (auto& pe : exceptions) {
    auto process_name = GetProcessName(pe);
    auto thread_name = GetThreadName(pe);
    printf("Exception! Process %s, Thread %s\n", process_name.c_str(), thread_name.c_str());
    fflush(stdout);
  }
}
