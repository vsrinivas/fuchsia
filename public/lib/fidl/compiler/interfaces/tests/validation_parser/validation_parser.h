// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_VALIDATION_PARSER_H_
#define MOJO_VALIDATION_PARSER_H_

#include <stdint.h>
#include <string.h>

// Parses a validation test read in as a bunch of characters.
//
// On success, returns a null pointer and a allocates a buffer pointed to
// by *data that must be freed by the caller. *data_len contains the buffer's
// size. *num_handles contains the number of handles found by the parser.
//
// On failure, returns a buffer containing an error message which must be freed
// by the caller, null in *data, and 0 in *data_len. *num_handles contains 0.
extern "C" __attribute__((visibility("default"))) char* ParseValidationTest(
    const char* input,    // Input
    size_t* num_handles,  // Output
    uint8_t** data,       // Output
    size_t* data_len);    // Output

#endif  // MOJO_VALIDATION_PARSER_H__
