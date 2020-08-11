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

  // If the address is at the first address of an inline routine, it's ambiguous whether the
  // virtual location is at the first instruction of the inlined function, or at the optimized-out
  // "call" to the inlined function. This will not apply when looking up a function by name, in
  // that case the returned function will be the requested one.
  //
  // See stack.h for a longer discussion about ambiguous inline locations.
  //
  //  * Generally if you're referring to the location from "outside" (say as the destination of a
  //    call instruction), then you'll want to use kOuter mode. If the function begins with an
  //    inline, the user will want to see the containing function and not some trivial helper
  //    function that ended up being the first thing it executed. In this case, the returned
  //    file/line will the call location for the first inline function.
  //
  //  * Alternatively, if you could think of being "at" that instruction (say as as the address of
  //    an exception), you will generally want to use "kInner" since this will be the symbol that
  //    actually generated the excepting instruction.
  enum class AmbiguousInline {
    kOuter,
    kInner,
  };
  AmbiguousInline ambiguous_inline = AmbiguousInline::kInner;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_RESOLVE_OPTIONS_H_
