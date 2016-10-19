// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_GLUE_KTRACE_KTRACE_H_
#define APPS_MOZART_GLUE_KTRACE_KTRACE_H_

#include <stdint.h>

// A lightweight C++ wrapper for the Magenta ktrace probe mechanism.

namespace ktrace {

// Creates a new trace probe id.
uint32_t TraceAddProbe(const char* name);

// Writes a probe to ktrace with optional arguments.
void TraceWriteProbe(uint32_t probe_id, uint32_t arg1 = 0u, uint32_t arg2 = 0u);

}  // namespace ktrace

#endif  // APPS_MOZART_GLUE_KTRACE_KTRACE_H_
