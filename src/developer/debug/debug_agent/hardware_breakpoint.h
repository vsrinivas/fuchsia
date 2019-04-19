// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>

#include <zircon/status.h>

namespace debug_agent {

class ProcessBreakpoint;

class HardwareBreakpoint {
 public:
  explicit HardwareBreakpoint(ProcessBreakpoint*);
  ~HardwareBreakpoint();

  // Checks if any of the installations need to be added/removed.
  zx_status_t Update(const std::set<zx_koid_t>& thread_koids);
  // Uninstall all the threads.
  zx_status_t Uninstall();

  const std::set<zx_koid_t>& installed_threads() const {
    return installed_threads_;
  }

 private:
  // Install/Uninstall a particular thread.
  zx_status_t Install(zx_koid_t thread_koid);
  zx_status_t Uninstall(zx_koid_t thread_koid);

  ProcessBreakpoint* process_bp_;  // Not-owning.
  std::set<zx_koid_t> installed_threads_;
};


// A given set of breakpoints have a number of locations, which could target
// different threads. We need to get all the threads that are targeted to
// this particular location.
std::set<zx_koid_t> HWThreadsTargeted(const ProcessBreakpoint& pb);

}  // namespace debug_agent
