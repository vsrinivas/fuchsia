// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_LOADBENCH_UTILITY_H_
#define SRC_TESTING_LOADBENCH_UTILITY_H_

#include <lib/zx/profile.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/profile.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

using double_seconds = std::chrono::duration<double, std::chrono::seconds::period>;

// Parses a duration in string form, which may include the units m, s, ms, us, or ns, and returns
// the equivalent value in nanoseconds.
std::chrono::nanoseconds ParseDurationString(const std::string& duration);

// Parses an expression of the form "cpu_num<+|-|*><positive integer>" and returns evaluated result
// as an integer.
size_t ParseInstancesString(const std::string& instances_str);

// Returns an unowned handle to a profile for the specified priority. Maintains
// an internal map of already requested profiles and returns the same handle for
// multiple requests for the same priority.
zx::unowned_profile GetProfile(int priority, std::optional<zx_cpu_set_t> affinity = std::nullopt);

// Returns an unowned handle to a profile for the specified deadline parameters.
// Maintains an internal map of already requested profiles and returns the same
// handle for multiple request for the same deadline parameters.
zx::unowned_profile GetProfile(zx::duration capacity, zx::duration deadline, zx::duration period,
                               std::optional<zx_cpu_set_t> affinity = std::nullopt);

// Returns an unowned handle to the root resource. Mantains an internal handle and returns the same
// value for multiple requests.
zx::unowned_resource GetRootResource();

// Returns the number of CPUs in the system.
size_t ReadCpuCount();

// Reads the CPU stats for the given number of CPUs.
void ReadCpuStats(zx_info_cpu_stats_t* stats_buffer, size_t record_count);

#endif  // SRC_TESTING_LOADBENCH_UTILITY_H_
