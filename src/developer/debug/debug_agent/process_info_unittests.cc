// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/process.h>
#include <lib/zx/thread.h>

#include "gtest/gtest.h"
#include "src/developer/debug/debug_agent/object_util.h"
#include "src/developer/debug/debug_agent/process_info.h"

namespace debug_agent {

TEST(ProcessInfo, GetProcessThreads) {
  zx_handle_t current_thread = zx_thread_self();
  zx_koid_t current_thread_koid = KoidForObject(current_thread);

  std::string old_name = NameForObject(current_thread);

  // Set the name of the current thread so we can find it.
  const std::string thread_name("ProcessInfo test thread name");
  zx_status_t status = zx_object_set_property(
      current_thread, ZX_PROP_NAME, thread_name.c_str(), thread_name.size());
  EXPECT_EQ(thread_name, NameForObject(current_thread));

  std::vector<debug_ipc::ThreadRecord> threads;
  status = GetProcessThreads(*zx::process::self(), 0, &threads);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_LT(0u, threads.size());

  bool found = false;
  for (const auto& thread : threads) {
    if (thread.koid == current_thread_koid && thread.name == thread_name) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);

  // Put back the old thread name for hygiene.
  zx_object_set_property(current_thread, ZX_PROP_NAME, old_name.c_str(),
                         old_name.size());
}

}  // namespace debug_agent
