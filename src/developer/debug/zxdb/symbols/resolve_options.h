// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_RESOLVE_OPTIONS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_RESOLVE_OPTIONS_H_

namespace zxdb {

// Options to pass to ResolveInputLocation that controls how symbols are converted to addresses.
struct ResolveOptions {
  // Set to true to symbolize the results. Otherwise the results will be just addresses.
  bool symbolize = true;

  // The function prologue is the instructions that set up the stack at the beginning. Many things
  // like stack traces and function parameter evaluation won't be consistenly available inside the
  // prologue.
  //
  // As a result, when setting breakpoints or stepping to the beginning of a function should
  // generally skip the prologue so the user sees consistent data. But if you want to know the
  // true range of a function to disassemble it or take its address, you'll want to include the
  // prologue.
  //
  // When set, queries will detect that the input is at the beginning of a function (regarless of
  // whether the InputLocation is specified as a function name, line, or address) and slide the
  // output address down to the first instruction following the prologue.
  //
  // Be careful: This option can have the effect that querying for the symbols for an address will
  // return a different address.
  //
  // THIS OPTION REQUIRES THAT symbolize == true. Query functions will assert otherwise since
  // reading the prologue requires symbolization.
  bool skip_function_prologue = false;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_RESOLVE_OPTIONS_H_
