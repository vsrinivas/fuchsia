// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sched.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <array>
#include <string>
#include <utility>

#include "args.h"

namespace sched {

zx_status_t CreateProfile(uint32_t priority, const std::string& name, zx::profile* profile) {
  std::unique_ptr<sys::ComponentContext> startup_context = sys::ComponentContext::Create();
  if (startup_context == nullptr) {
    return ZX_ERR_UNAVAILABLE;
  }
  fuchsia::scheduler::ProfileProviderSyncPtr profile_provider;
  zx_status_t status = startup_context->svc()->Connect(profile_provider.NewRequest());
  if (status != ZX_OK) {
    return status;
  }
  zx_status_t server_status;
  status = profile_provider->GetProfile(priority, name, &server_status, profile);
  if (status != ZX_OK) {
    return status;
  }
  return server_status;
}

zx_status_t Launch(zx_handle_t job, const std::vector<std::string>& args, zx::process* process_out,
                   std::string* error_message) {
  std::array<char, FDIO_SPAWN_ERR_MSG_MAX_LENGTH> err_msg;

  // Convert our vector of strings into an array or const char* pointers.
  std::vector<const char*> argv;
  argv.reserve(args.size() + 1);
  for (const std::string& arg : args) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);

  // Spawn the new job.
  zx_status_t result =
      fdio_spawn_etc(job, FDIO_SPAWN_CLONE_ALL, args[0].c_str(), argv.data(), nullptr, 0, nullptr,
                     process_out->reset_and_get_address(), err_msg.data());
  if (result != ZX_OK) {
    *error_message = std::string(err_msg.data());
    return result;
  }

  return ZX_OK;
}

zx_status_t ApplyProfileToProcess(const zx::process& process, const zx::profile& profile,
                                  bool verbose) {
  // Find all threads in the given process.
  constexpr size_t kMaxThreads = 16;
  std::array<zx_koid_t, kMaxThreads> buffer;
  size_t num_threads;
  size_t num_fetched;
  zx_status_t info_result = process.get_info(ZX_INFO_PROCESS_THREADS, buffer.data(), buffer.size(),
                                             &num_threads, &num_fetched);
  if (info_result != ZX_OK) {
    return info_result;
  }
  if (verbose) {
    printf("sched: Found %ld thread(s) in child process.\n", num_threads);
  }

  // Ensure we found at least 1 thread.
  if (num_fetched == 0) {
    return ZX_ERR_BAD_STATE;
  }

  // Apply the profile to each one.
  zx_status_t final_result = ZX_OK;
  for (size_t i = 0; i < num_fetched; i++) {
    // Get handle to the thread.
    zx::thread thread;
    zx_status_t result = process.get_child(buffer[i], ZX_RIGHT_SAME_RIGHTS, &thread);
    if (result != ZX_OK) {
      fprintf(stderr, "sched: Error fetching child thread handle: %s\n",
              zx_status_get_string(result));
      final_result = result;
      continue;
    }

    // Apply the profile.
    result = thread.set_profile(profile, /*options=*/0);
    if (result != ZX_OK) {
      fprintf(stderr, "sched: Could not apply profile to thread: %s\n",
              zx_status_get_string(result));
      final_result = result;
      continue;
    }
    if (verbose) {
      printf("sched: Successfully applied profile to TID %d\n", thread.get());
    }
  }

  return final_result;
}

int Run(int argc, const char** argv) {
  // Parse arguments.
  CommandLineArgs args = ParseArgsOrExit(argc, argv);

  // Create a profile with the given arguments.
  zx::profile profile;
  zx_status_t result = CreateProfile(args.priority, "sched", &profile);
  if (result != ZX_OK) {
    fprintf(stderr, "Error creating Zircon profile object: %s\n", zx_status_get_string(result));
    return 1;
  }

  // Launch the given command.
  std::string error_message;
  zx::process process;
  result = Launch(ZX_HANDLE_INVALID, args.params, &process, &error_message);
  if (result != ZX_OK) {
    fprintf(stderr, "Could not run command: %s (error %s)\n", error_message.c_str(),
            zx_status_get_string(result));
    return 1;
  }
  if (args.verbose) {
    printf("Launched child process %d\n", process.get());
  }

  // Apply the profile.
  result = ApplyProfileToProcess(process, profile, args.verbose);
  if (result != ZX_OK) {
    fprintf(stderr, "sched: Could not apply profile to threads in process: %s\n",
            zx_status_get_string(result));
    // We continue running the child application anyway.
  }

  // Wait for process to finish.
  process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  if (args.verbose) {
    printf("Child process terminated.\n");
  }

  return 0;
}

}  // namespace sched
