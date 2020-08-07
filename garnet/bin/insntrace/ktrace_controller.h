// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_INSNTRACE_KTRACE_CONTROLLER_H_
#define GARNET_BIN_INSNTRACE_KTRACE_CONTROLLER_H_

#include <stdint.h>

#include <fuchsia/tracing/kernel/cpp/fidl.h>

namespace insntrace {

bool OpenKtraceChannel(fuchsia::tracing::kernel::ControllerSyncPtr* out_controller_ptr);

bool RequestKtraceStart(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace,
                        uint32_t group_mask);

void RequestKtraceStop(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace);

void RequestKtraceRewind(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace);

void DumpKtraceBuffer(const char* output_path_prefix, const char* output_path_suffix);

}  // namespace insntrace

#endif  // GARNET_BIN_INSNTRACE_KTRACE_CONTROLLER_H_
