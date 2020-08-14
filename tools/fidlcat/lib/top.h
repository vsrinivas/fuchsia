// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_TOP_H_
#define TOOLS_FIDLCAT_LIB_TOP_H_

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

class Top {
 public:
  explicit Top(SyscallDisplayDispatcher* dispatcher) : dispatcher_(dispatcher) {}
  void Display(std::ostream& os);
  void DisplayProcessContent(FidlcatPrinter& printer, Process* process);
  void DisplayProtocolContent(FidlcatPrinter& printer, Protocol* protocol);

 private:
  SyscallDisplayDispatcher* dispatcher_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_TOP_H_
