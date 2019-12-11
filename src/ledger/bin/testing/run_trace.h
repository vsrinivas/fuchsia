// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_RUN_TRACE_H_
#define SRC_LEDGER_BIN_TESTING_RUN_TRACE_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <string>
#include <vector>

namespace ledger {

// This path is added to trace's namespace and points to our "/pkg/data".
constexpr char kTraceTestDataLocalPath[] = "/pkg/data";
constexpr char kTraceTestDataRemotePath[] = "/test_data";

// Run the trace program as a component, passing it |argv|, which is NULL-terminated.
// A path to the calling component's /pkg directory is passed to the trace program as
// |kTestPkgPath|.
void RunTrace(sys::ComponentContext* component_context,
              fuchsia::sys::ComponentControllerPtr* component_controller,
              const std::vector<std::string>& argv);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_RUN_TRACE_H_
