// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STATUSOR_STATUS_MACROS_H_
#define LIB_STATUSOR_STATUS_MACROS_H_

// Takes an object that has a status() method, and prints an error message and returns a the status
// if it's not ZX_OK.
#define RETURN_IF_ERROR(func, err_msg)      \
  do {                                      \
    zx_status_t _status = (func).status();  \
    if (_status != ZX_OK) {                 \
      printf("%s: %d\n", err_msg, _status); \
      return _status;                       \
    }                                       \
  } while (false)

// Hack needed to expand __COUNTER__ before concatenating.
#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)

#define ASSIGN_OR_RETURN_IMPL(sname, lhs, func, err_msg) \
  auto sname = (func);                                   \
  if (!sname.ok()) {                                     \
    printf("%s: %d\n", err_msg, sname.status());         \
    return sname.status();                               \
  }                                                      \
  lhs = sname.ValueOrDie()

// Executes a expression that returns an object o with a status() method, and either prints a
// message and returns if the status isn't ZX_OK, or assigns o.ValueOrDie() to lhs.
//
// Use like:
// ASSIGN_OR_RETURN(auto x, FunctionThatReturnsStatus(), "Error message");
#define ASSIGN_OR_RETURN(lhs, func, err_msg) \
  ASSIGN_OR_RETURN_IMPL(MACRO_CONCAT(sName, __COUNTER__), lhs, func, err_msg)

#endif  // LIB_STATUSOR_STATUS_MACROS_H_
