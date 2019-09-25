// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Methods to set and parse command-line flags used by the ledger app.

#ifndef SRC_LEDGER_BIN_APP_FLAGS_H_
#define SRC_LEDGER_BIN_APP_FLAGS_H_

#include <fuchsia/sys/cpp/fidl.h>

#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/command_line.h"

namespace ledger {

// The default garbage-collection policy when starting Ledger.
inline constexpr storage::GarbageCollectionPolicy kDefaultGarbageCollectionPolicy =
    storage::GarbageCollectionPolicy::NEVER;

// The garbage-collection policy to use for tests. This does not include benchmarks, which should
// use the default garbage collection policy instead to provide realistic performance numbers.
inline constexpr storage::GarbageCollectionPolicy kTestingGarbageCollectionPolicy =
    storage::GarbageCollectionPolicy::EAGER_LIVE_REFERENCES;

// Parses |command_line| to extract the garbage-collection policy.
storage::GarbageCollectionPolicy GarbageCollectionPolicyFromFlags(
    const fxl::CommandLine& command_line);

// Appends command-line flags representing |policy| to |launch_info| arguments.
void AppendGarbageCollectionPolicyFlags(storage::GarbageCollectionPolicy policy,
                                        fuchsia::sys::LaunchInfo* launch_info);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_FLAGS_H_
