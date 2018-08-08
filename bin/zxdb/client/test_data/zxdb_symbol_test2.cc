// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/test_data/zxdb_symbol_test.h"

class ClassInTest2 {
  EXPORT static int FunctionInTest2();
};

// The symbols we look up need to be in at least two different compilation
// units (i.e. source .cc files) so the test can validate unit-relative
// addressing (otherwise all unit-relative addresses are also valid global
// addresses).
int ClassInTest2::FunctionInTest2() { return 99; }
