// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To use this file, define a macro like
//
//   #define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) ...
//
// before including this file.
//
// This file defines options with non-literal default values, and includes options-literal.inc.

#ifndef FUCHSIA_FUZZER_OPTION
#error "FUCHSIA_FUZZER_OPTION must be defined before including this file."
#endif

//                   (type,             snake_case_name,    CamelCaseName,    default-value);
FUCHSIA_FUZZER_OPTION(SanitizerOptions, sanitizer_options,  SanitizerOptions, SanitizerOptions())

#include "src/sys/fuzzing/common/options-literal.inc"
