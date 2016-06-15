// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <unittest.h>

void unittests()
{
    bool success = run_all_tests();
    printf(success ? "Success\n" : "Failure\n");
}
