// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/limbo_provider.h"

using namespace fuchsia::exception;

namespace debug_agent {

LimboProvider::~LimboProvider() = default;

zx_status_t LimboProvider::ListProcessesOnLimbo(std::vector<ProcessExceptionMetadata>* out) {
  std::vector<ProcessExceptionMetadata> exceptions;

  ProcessLimboSyncPtr process_limbo;
  zx_status_t status = process_limbo->ListProcessesWaitingOnException(&exceptions);
  if (status != ZX_OK)
    return status;

  *out = std::move(exceptions);
  return ZX_OK;
}

}  // namespace debug_agent
