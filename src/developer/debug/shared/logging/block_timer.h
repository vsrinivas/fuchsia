// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sstream>

#include "src/developer/debug/shared/logging/file_line_function.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/time/stopwatch.h"

namespace debug_ipc {

// BlockTimer ------------------------------------------------------------------

// Simple RAII-esque timer that prints the duration of a block if running on
// debug mode.
//
// Normally you would use it from the TIME_BLOCK macro (defined below), that
// will easily add the current calling site, but you can add your own locations
// in order to proxy calls (see message_loop.cc for an example).
class BlockTimer {
 public:
  BlockTimer(FileLineFunction origin);
  ~BlockTimer();  // Will log on destruction.

  // This is what get called on destruction. You can call it before destruction
  // to trigger the timer before that. Will not trigger again.
  // Returns the timing (in milliseconds).
  double EndTimer();

  // BlockTimers should only measure the block they're in. No weird stuff.
  FXL_DISALLOW_COPY_AND_ASSIGN(BlockTimer);
  FXL_DISALLOW_MOVE(BlockTimer);

  std::ostream& stream() { return stream_; }

 private:
  FileLineFunction origin_;  // Where this timer was called from.
  fxl::Stopwatch timer_;
  bool should_log_;

  std::ostringstream stream_;
};

// We use this macro to ensure the concatenation of the values. Oh macros :)
#define TIME_BLOCK_TOKEN(x, y) x##y
#define TIME_BLOCK_TOKEN2(x, y) TIME_BLOCK_TOKEN(x, y)

// Meant to be used at a scope.
// Foo() {
//  TIME_BLOCK() << "Timing on Foo description.";
//  ...
//  <REST OF FUNCTION>
//  ...
// }  <-- Logs the timing on the destructor.
#define TIME_BLOCK() \
  TIME_BLOCK_WITH_NAME(TIME_BLOCK_TOKEN2(__timer__, __LINE__))

// Useful for calling timing on code that is not easily "scopable":
//
// TIME_BLOCK_WITH_NAME(timer_name) << "Some description.";
// ...
// <CODE TO BE TIMED>
// ...
// double time_in_ms = timer_name.EndTimer();
// DoSomethingWithTiming(time_is_ms);
#define TIME_BLOCK_WITH_NAME(var_name)         \
  ::debug_ipc::BlockTimer var_name(FROM_HERE); \
  var_name.stream()

}  // namespace debug_ipc
