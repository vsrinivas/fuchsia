// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "get-int.h"

int GetInt() {
  // Exercise some weirdness first to ensure that this module has been
  // properly loaded.
  static int data_location;
  static int* volatile data_address = &data_location;
  static int* const volatile relro_address = &data_location;

  // This makes absolutely sure the compiler doesn't think it knows how to
  // optimize away the fetches and tests.
  int* from_data;
  __asm__("" : "=g"(from_data) : "0"(data_address));

  // Since data_location has internal linkage, the references here will use
  // pure PC-relative address materialization.
  if (from_data != &data_location) {
    // Address in data not relocated properly.
    return -1;
  }

  int* from_relro;
  __asm__("" : "=g"(from_relro) : "0"(relro_address));
  if (from_relro != &data_location) {
    // Address in RELRO not relocated properly.
    return -2;
  }

  return 42;
}
