// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_CALLBACK_DESTRUCTION_SENTINEL_H_
#define SRC_LEDGER_LIB_CALLBACK_DESTRUCTION_SENTINEL_H_

#include <lib/fit/function.h>

namespace ledger {

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
  DestructionSentinel(const DestructionSentinel&) = delete;
  DestructionSentinel& operator=(const DestructionSentinel&) = delete;
  ~DestructionSentinel();

  // Executes |closure| and returns |true| if the sentinel has been destroyed
  // while executing it.
  inline bool DestructedWhile(const fit::closure& closure) {
    bool is_destructed = false;
    bool* old_is_destructed_ptr = is_destructed_ptr_;
    is_destructed_ptr_ = &is_destructed;
    closure();
    if (is_destructed) {
      if (old_is_destructed_ptr)
        *old_is_destructed_ptr = true;
      return true;
    }
    is_destructed_ptr_ = old_is_destructed_ptr;
    return false;
  };

 private:
  bool* is_destructed_ptr_ = nullptr;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_CALLBACK_DESTRUCTION_SENTINEL_H_
