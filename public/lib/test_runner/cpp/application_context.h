// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_LIB_APPLICATION_CONTEXT_H_
#define APPS_TEST_RUNNER_LIB_APPLICATION_CONTEXT_H_

#include "lib/app/cpp/application_context.h"

namespace test_runner {
// Returns an |app::ApplicationContext| singleton.
app::ApplicationContext* GetApplicationContext();
}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_LIB_APPLICATION_CONTEXT_H_
