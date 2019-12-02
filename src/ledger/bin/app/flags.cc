// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/flags.h"

#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"

ABSL_FLAG(storage::GarbageCollectionPolicy, gc_policy, ledger::kDefaultGarbageCollectionPolicy,
          "default garbage collection policy");

namespace ledger {

void AppendGarbageCollectionPolicyFlags(storage::GarbageCollectionPolicy policy,
                                        fuchsia::sys::LaunchInfo* launch_info) {
  if (!launch_info->arguments.has_value()) {
    launch_info->arguments = std::vector<std::string>{};
  }
  launch_info->arguments->push_back(
      absl::StrCat("--", FLAGS_gc_policy.Name(), "=", AbslUnparseFlag(policy)));
};

}  // namespace ledger
