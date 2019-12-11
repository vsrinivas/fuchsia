// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_RUN_ALL_UNITTESTS_H_
#define SRC_LEDGER_BIN_TESTING_RUN_ALL_UNITTESTS_H_

namespace ledger {

// Intializes gtest, logging and the test loop with the given arguments, then runs all tests.
int RunAllUnittests(int argc, char** argv);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_RUN_ALL_UNITTESTS_H_
