// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys-unittests.h"

#include <stdio.h>

#include <ktl/span.h>

#include "phys-unittest.h"
#include "test-main.h"

#include <ktl/enforce.h>

// This isn't more straightforwardly table-driven because even as a
// function-local variable the compiler will try to turn the table
// into a const global with relocations.

TEST_SUITES(                    //
    "phys-unittests",           //
    stack_tests,                //
    relocation_tests,           //
    popcount_tests,             //
    printf_tests,               //
    string_view_tests,          //
    string_file_tests,          //
    multi_file_tests,           //
    unittest_tests,             //
    zbitl_tests,                //
    ArchRandomTests,            //
    crypto_entropy_pool_tests,  //
    boot_option_tests           //
)
