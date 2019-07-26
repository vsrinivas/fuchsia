// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_TESTS_INTEL_TESTS_H_
#define GARNET_BIN_CPUPERF_TESTS_INTEL_TESTS_H_

#include "../verify_test.h"

// These are defined in each testcase's file, and referenced by the
// testcase verifier.
extern const TestSpec kFixedCounterSpec;
extern const TestSpec kLastBranchSpec;
extern const TestSpec kOsFlagSpec;
extern const TestSpec kProgrammableCounterSpec;
extern const TestSpec kTallySpec;
extern const TestSpec kUserFlagSpec;
extern const TestSpec kValueRecordsSpec;

#endif  // GARNET_BIN_CPUPERF_TESTS_INTEL_TESTS_H_
