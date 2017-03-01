// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): wip wip wip

#pragma once

#include "ipt-server.h"

namespace debugserver {

bool SetPerfMode(const PerfConfig& config);

bool InitCpuPerf(const PerfConfig& config);

bool InitThreadPerf(Thread* thread, const PerfConfig& config);

bool InitPerfPreProcess(const PerfConfig& config);

bool StartCpuPerf(const PerfConfig& config);

bool StartThreadPerf(Thread* thread, const PerfConfig& config);

void StopCpuPerf(const PerfConfig& config);

void StopThreadPerf(Thread* thread, const PerfConfig& config);

void StopPerf(const PerfConfig& config);

void DumpCpuPerf(const PerfConfig& config);

void DumpThreadPerf(Thread* thread, const PerfConfig& config);

void DumpPerf(const PerfConfig& config);

void ResetCpuPerf(const PerfConfig& config);

void ResetThreadPerf(Thread* thread, const PerfConfig& config);

void ResetPerf(const PerfConfig& config);

} // debugserver namespace
