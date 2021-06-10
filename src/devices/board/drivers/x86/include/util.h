// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_UTIL_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_UTIL_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <acpica/acpi.h>

// TODO(fxbug.dev/78349): delete these methods once users are in their own drivers.
ACPI_STATUS acpi_evaluate_integer(ACPI_HANDLE handle, const char* name, uint64_t* out);
ACPI_STATUS acpi_evaluate_method_intarg(ACPI_HANDLE handle, const char* name, uint64_t arg);

// ACPI tables and objects in ACPI tables are identified using the their
// "signature" which is defined in section 5.2 of the ACPI specification as a
// "fixed length string".  The majority of these signatures are 4 byte, non-null
// terminated, fixed length strings.  The ACPICA library stores these signatures
// in their data structures as packed uint32_t's which are read from the tables
// in a little endian fashion.
//
// This effectively makes the signatures we need to deal with in ACPICA into
// FourCC codes
//
// https://en.wikipedia.org/wiki/FourCC
//
// Basically, 4 characters strings which are packed into and manipulated as
// integers.  So, here we define a couple of utility functions that make it a
// bit easier to deal with these FourCC identifies in our code.
//
// make_fourcc allows us to build a FourCC in a constexpr fashion so that we can
// then easily test object names simply by saying something like
//
//   if (obj->Name == make_fourcc('D', 'S', 'D', 'T')) { ... }
//
// For diagnostic and debug messages, it is frequently advantageous to have a
// properly null terminated string representation, but one which does not need
// to be heap allocated.  For that, we haave fourcc_to_string which will pack
// the characters of a u32 FourCC into a 5-byte character array which can exist
// on the stack during a call to a logging function.  For example
//
//   zxlogf(INFO, "Found object %s", fourcc_to_string(obj->Name).str);
//
constexpr inline uint32_t make_fourcc(char a, char b, char c, char d) {
  return ((static_cast<uint32_t>(d) & 0xFF) << 24) | ((static_cast<uint32_t>(c) & 0xFF) << 16) |
         ((static_cast<uint32_t>(b) & 0xFF) << 8) | (static_cast<uint32_t>(a) & 0xFF);
}

inline auto fourcc_to_string(uint32_t fourcc) {
  struct Ret {
    char str[5];
  };

  char a = static_cast<char>((fourcc >> 0) & 0xFF);
  char b = static_cast<char>((fourcc >> 8) & 0xFF);
  char c = static_cast<char>((fourcc >> 16) & 0xFF);
  char d = static_cast<char>((fourcc >> 24) & 0xFF);
  return Ret{.str = {isprint(a) ? a : '.', isprint(b) ? b : '.', isprint(c) ? c : '.',
                     isprint(d) ? d : '.', 0x00}};
}

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_UTIL_H_
