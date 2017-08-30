// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/interfaces/bindings/tests/validation_parser/validation_parser.h"

#include <stdlib.h>

#include "mojo/public/cpp/bindings/tests/validation_test_input_parser.h"

// C interface for the validation test parser.
//
// This routine malloc()s return error and space for data which
// must be freed by the caller. Returns a null pointer in *data
// and a message in *(return value) on failure. Returns a valid
// pointer and size in *data and *data_len on success, and a
// null pointer in *(return value).
extern "C" char* ParseValidationTest(const char* input,    // Input
                                     size_t* num_handles,  // Output
                                     uint8_t** data,       // Output
                                     size_t* data_len)     // Output
{
  // C++ interface
  std::string cpp_input(input);
  std::vector<uint8_t> cpp_data;
  std::string error_message;
  // Call the parser
  if (!mojo::test::ParseValidationTestInput(cpp_input, &cpp_data, num_handles,
                                            &error_message)) {
    // Allocate buffer to return error string
    // Add 1 to allocation for null terminator
    int len = error_message.size() + 1;
    char* ret_err = (char*)malloc(len);
    strncpy(ret_err, error_message.c_str(), len);
    // Set data to null and size to 0 since we failed.
    *data = nullptr;
    *data_len = 0;
    return ret_err;
  }
  int cpp_data_size = cpp_data.size();
  if (cpp_data_size != 0) {
    // Allocate a buffer for the returned data.
    *data = (uint8_t*)malloc(cpp_data_size);
    *data_len = cpp_data_size;
    // Copy the parsed output into the buffer.
    memcpy(*data, cpp_data.data(), cpp_data_size);
  } else {
    // If we have no data, set the pointer to null and size to 0.
    *data = nullptr;
    *data_len = 0;
  }
  return nullptr;
}
