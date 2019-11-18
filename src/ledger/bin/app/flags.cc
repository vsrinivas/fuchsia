// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/flags.h"

#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace ledger {
namespace {
constexpr fxl::StringView kGcPolicyFlag = "gc_policy";
constexpr fxl::StringView kNeverPolicy = "never";
constexpr fxl::StringView kEagerPolicy = "eager";
constexpr fxl::StringView kRootNodesPolicy = "root_nodes";

}  // namespace

storage::GarbageCollectionPolicy GarbageCollectionPolicyFromFlags(
    const fxl::CommandLine& command_line) {
  std::string policy;
  if (!command_line.GetOptionValue(kGcPolicyFlag, &policy)) {
    return kDefaultGarbageCollectionPolicy;
  }
  if (policy == kNeverPolicy) {
    return storage::GarbageCollectionPolicy::NEVER;
  } else if (policy == kEagerPolicy) {
    return storage::GarbageCollectionPolicy::EAGER_LIVE_REFERENCES;
  } else if (policy == kRootNodesPolicy) {
    return storage::GarbageCollectionPolicy::EAGER_ROOT_NODES;
  } else {
    FXL_LOG(FATAL) << "Invalid --" << kGcPolicyFlag << " value: " << policy;
    // The above line will kill the process, this return is just a sane default to make the compiler
    // happy.
    return kDefaultGarbageCollectionPolicy;
  }
}

void AppendGarbageCollectionPolicyFlags(storage::GarbageCollectionPolicy policy,
                                        fuchsia::sys::LaunchInfo* launch_info) {
  fxl::StringView flag;
  switch (policy) {
    case storage::GarbageCollectionPolicy::NEVER:
      flag = kNeverPolicy;
      break;
    case storage::GarbageCollectionPolicy::EAGER_LIVE_REFERENCES:
      flag = kEagerPolicy;
      break;
    case storage::GarbageCollectionPolicy::EAGER_ROOT_NODES:
      flag = kRootNodesPolicy;
      break;
  }
  if (!launch_info->arguments.has_value()) {
    launch_info->arguments = std::vector<std::string>{};
  }
  launch_info->arguments->push_back(fxl::Concatenate({"--", kGcPolicyFlag, "=", flag}));
};

}  // namespace ledger
