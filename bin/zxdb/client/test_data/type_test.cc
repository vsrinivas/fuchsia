// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/test_data/zxdb_symbol_test.h"

// This file is compiled into a library and used in the DWARFSymboLFactory
// tests to query symbol information. The actual code is not run.

EXPORT const int* GetIntPtr() { return nullptr; }  // Line 10.
