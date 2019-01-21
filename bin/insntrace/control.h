// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): wip wip wip

#pragma once

#include "server.h"

namespace insntrace {

bool AllocTrace(const IptConfig& config);

bool InitTrace(const IptConfig& config);

bool InitThreadTrace(inferior_control::Thread* thread, const IptConfig& config);

bool InitProcessTrace(const IptConfig& config);

bool StartTrace(const IptConfig& config);

bool StartThreadTrace(inferior_control::Thread* thread, const IptConfig& config);

void StopTrace(const IptConfig& config);

void StopThreadTrace(inferior_control::Thread* thread, const IptConfig& config);

void StopSidebandDataCollection(const IptConfig& config);

void DumpTrace(const IptConfig& config);

void DumpThreadTrace(inferior_control::Thread* thread, const IptConfig& config);

void DumpSidebandData(const IptConfig& config);

void ResetTrace(const IptConfig& config);

void ResetThreadTrace(inferior_control::Thread* thread, const IptConfig& config);

void FreeTrace(const IptConfig& config);

}  // namespace insntrace
