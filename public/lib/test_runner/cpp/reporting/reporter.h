// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_LIB_REPORTER_H_
#define APPS_TEST_RUNNER_LIB_REPORTER_H_

#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/test_runner/fidl/test_runner.fidl.h"

namespace test_runner {

void ReportResult(std::string identity,
                  app::ApplicationContext* context,
                  std::vector<TestResultPtr> results);

}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_LIB_REPORTER_H_
