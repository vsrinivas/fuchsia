// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SCHED_SCHED_H_
#define GARNET_BIN_SCHED_SCHED_H_

#include <lib/zx/process.h>
#include <lib/zx/profile.h>
#include <stdint.h>
#include <zircon/status.h>

#include <string>

namespace sched {

// Create a Zircon profile object.
zx_status_t CreateProfile(uint32_t priority, const std::string& name, zx::profile* profile);

// Launch the given command line application.
zx_status_t Launch(zx_handle_t job, const std::vector<std::string>& args, zx::process* process_out,
                   std::string* error_message);

// Apply the given profile to all threads currently running in the given process.
//
// This will have no effect for threads spawned after this call completes.
zx_status_t ApplyProfileToProcess(const zx::process& process, const zx::profile& profile,
                                  bool verbose);

// Run the main binary with the given command line args.
int Run(int argc, const char** argv);

}  // namespace sched

#endif  // GARNET_BIN_SCHED_SCHED_H_
