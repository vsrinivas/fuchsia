// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_BENCHMARK_LIBUNWINDSTACK_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_BENCHMARK_LIBUNWINDSTACK_H_

#include "src/developer/debug/unwinder/benchmark/minidump_memory.h"
#include "src/developer/debug/unwinder/unwind.h"

namespace benchmark {

std::vector<unwinder::Frame> UnwindFromLibunwindstack(
    const std::shared_ptr<MinidumpMemory>& memory,
    std::vector<const crashpad::ModuleSnapshot*> modules, const crashpad::ThreadSnapshot* thread);

}

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_BENCHMARK_LIBUNWINDSTACK_H_
