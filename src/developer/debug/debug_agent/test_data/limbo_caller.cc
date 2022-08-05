// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fsl/handles/object_info.h"

// Simple application that obtains the Process Limbo services and obtains the exceptions from it.
// Meant to be called manually for testing purposes.

namespace {

std::string GetProcessName(const fuchsia::exception::ProcessExceptionMetadata& pe) {
  if (pe.has_process())
    return fsl::GetObjectName(pe.process().get());
  return {};
}

std::string GetThreadName(const fuchsia::exception::ProcessExceptionMetadata& pe) {
  if (pe.has_thread())
    return fsl::GetObjectName(pe.thread().get());
  return {};
}

// Program executing in the `ffx component explore` environment don't receive
// capabilities in the standard path at `/`. Instead, the process access the
// scoped component's namespace via the `/ns` directory. Consequently, when
// the service root is created, the protocols are not located in `/svc`,
// the default for most components, but rather at `/ns/svc`.
zx::channel OpenServiceRoot() {
  zx::channel request, service_root;
  auto status = zx::channel::create(0, &request, &service_root);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  status = fdio_service_connect("/ns/svc", request.release());
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  return service_root;
}

}  // namespace

int main() {
  auto environment_services = std::make_shared<sys::ServiceDirectory>(OpenServiceRoot());

  fuchsia::exception::ProcessLimboSyncPtr process_limbo;
  environment_services->Connect(process_limbo.NewRequest());

  fuchsia::exception::ProcessLimbo_WatchProcessesWaitingOnException_Result result;
  zx_status_t status = process_limbo->WatchProcessesWaitingOnException(&result);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  FX_DCHECK(result.is_response()) << zx_status_get_string(result.err());

  auto& exceptions = result.response().exception_list;
  printf("Got %zu exceptions.\n", exceptions.size());
  fflush(stdout);
  for (auto& pe : exceptions) {
    auto process_name = GetProcessName(pe);
    auto thread_name = GetThreadName(pe);
    printf("Exception! Process %s, Thread %s\n", process_name.c_str(), thread_name.c_str());
    fflush(stdout);
  }
}
