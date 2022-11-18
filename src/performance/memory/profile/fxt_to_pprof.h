// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_MEMORY_PROFILE_FXT_TO_PPROF_H_
#define SRC_PERFORMANCE_MEMORY_PROFILE_FXT_TO_PPROF_H_

#include <lib/fit/result.h>

#include <functional>
#include <string>

#include <trace-reader/records.h>

#include "src/performance/memory/profile/profile.pb.h"
#include "src/performance/memory/profile/record_container.h"

fit::result<std::string, perfetto::third_party::perftools::profiles::Profile> fxt_to_profile(
    const RecordContainer& records, const char* category);

#endif  // SRC_PERFORMANCE_MEMORY_PROFILE_FXT_TO_PPROF_H_
