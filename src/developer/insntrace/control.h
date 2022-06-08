// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_INSNTRACE_CONTROL_H_
#define SRC_DEVELOPER_INSNTRACE_CONTROL_H_

#include "src/developer/insntrace/config.h"

namespace insntrace {

bool AllocTrace(const IptConfig& config);

bool InitTrace(const IptConfig& config);

bool InitProcessTrace(const IptConfig& config);

bool StartTrace(const IptConfig& config);

void StopTrace(const IptConfig& config);

void StopSidebandDataCollection(const IptConfig& config);

void DumpTrace(const IptConfig& config);

void DumpSidebandData(const IptConfig& config);

void ResetTrace(const IptConfig& config);

void FreeTrace(const IptConfig& config);

}  // namespace insntrace

#endif  // SRC_DEVELOPER_INSNTRACE_CONTROL_H_
