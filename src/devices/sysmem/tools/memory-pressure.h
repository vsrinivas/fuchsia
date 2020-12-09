// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_SYSMEM_TOOLS_MEMORY_PRESSURE_H_
#define SRC_DEVICES_SYSMEM_TOOLS_MEMORY_PRESSURE_H_

#include "src/lib/fxl/command_line.h"

void Log(const char* format, ...);
void LogError(const char* format, ...);

void FlushLog(bool error);

int MemoryPressureCommand(fxl::CommandLine command_line, bool sleep);

#endif  // SRC_DEVICES_SYSMEM_TOOLS_MEMORY_PRESSURE_H_
