// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/arraysize.h>

#include "intel_tests.h"

// List of tests.
// A test automatically fails if it's not listed here.
extern const TestSpec* const kTestSpecs[] = {
  &kFixedCounterSpec,
  &kLastBranchSpec,
  &kOsFlagSpec,
  &kProgrammableCounterSpec,
  &kTallySpec,
  &kUserFlagSpec,
  &kValueRecordsSpec,
};

extern const size_t kTestSpecCount = arraysize(kTestSpecs);
