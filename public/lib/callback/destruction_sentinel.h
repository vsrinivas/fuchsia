// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_DESTRUCTION_SENTINEL_H_
#define LIB_CALLBACK_DESTRUCTION_SENTINEL_H_

#include <lib/fit/function.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace callback {

// Helper class to determine if a class has been deleted while running some
// code.
//
// To use a DestructionSentinel, one must add a DestructionSentinel member
// to the class susceptible to be deleted while running code. Any code
// susceptible to delete the class must be run inside the |DestructedWhile|
// method, and the method must do an early return if |DestructedWhile| returns
// |true|.
class DestructionSentinel {
 public:
  DestructionSentinel();
  ~DestructionSentinel();

  // Executes |closure| and returns |true| if the sentinel has been destroyed
  // while executing it.
  inline bool DestructedWhile(const fit::closure& closure) {
    FXL_DCHECK(!is_destructed_ptr_) << "DestructionSentinel is not reentrant. "
                                       "Please fix if reentrance is needed.";
    bool is_destructed = false;
    is_destructed_ptr_ = &is_destructed;
    closure();
    if (is_destructed)
      return true;
    is_destructed_ptr_ = nullptr;
    return false;
  };

 private:
  bool* is_destructed_ptr_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(DestructionSentinel);
};

}  // namespace callback

#endif  // LIB_CALLBACK_DESTRUCTION_SENTINEL_H_
