// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): wip wip wip

#pragma once

#include "server.h"

namespace insntrace {

bool AllocTrace(const IptConfig& config);

bool InitCpuPerf(const IptConfig& config);

bool InitThreadPerf(inferior_control::Thread* thread, const IptConfig& config);

bool InitPerfPreProcess(const IptConfig& config);

bool StartCpuPerf(const IptConfig& config);

bool StartThreadPerf(inferior_control::Thread* thread, const IptConfig& config);

void StopCpuPerf(const IptConfig& config);

void StopThreadPerf(inferior_control::Thread* thread, const IptConfig& config);

void StopPerf(const IptConfig& config);

void DumpCpuPerf(const IptConfig& config);

void DumpThreadPerf(inferior_control::Thread* thread, const IptConfig& config);

void DumpPerf(const IptConfig& config);

void ResetCpuPerf(const IptConfig& config);

void ResetThreadPerf(inferior_control::Thread* thread, const IptConfig& config);

void FreeTrace(const IptConfig& config);

}  // namespace insntrace
