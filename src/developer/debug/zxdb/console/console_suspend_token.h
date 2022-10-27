// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_SUSPEND_TOKEN_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_SUSPEND_TOKEN_H_

#include "src/developer/debug/zxdb/console/console_suspend_token.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class ConsoleImpl;
class MockConsole;

// A scoped class that can suspend the console's input.
//
// See Console::SuspendInput(). That will disable input and return this token which allows input
// to be explicitly enabled, or it will automatically happen when this class goes out of scope.
class ConsoleSuspendToken : public fxl::RefCountedThreadSafe<ConsoleSuspendToken> {
 public:
  void Enable() {
    if (!enabled_) {
      Console::get()->EnableInput();
      enabled_ = true;
    }
  }

  // Returns true if the console input has already been re-enabled.
  bool enabled() const { return enabled_; }

 private:
  friend ConsoleImpl;
  friend MockConsole;
  FRIEND_REF_COUNTED_THREAD_SAFE(ConsoleSuspendToken);

  // This is created by the Console object which will disable input when it creates us. So there's
  // nothing to do on initialization.
  ConsoleSuspendToken() {}

  ~ConsoleSuspendToken() {
    if (!enabled_)
      Enable();
  }

  bool enabled_ = false;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_SUSPEND_TOKEN_H_
