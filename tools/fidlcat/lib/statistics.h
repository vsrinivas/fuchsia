// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_STATISTICS_H_
#define TOOLS_FIDLCAT_LIB_STATISTICS_H_

#include "src/lib/fidl_codec/visitor.h"

namespace fidlcat {

class OutputEvent;
class SyscallDisplayDispatcher;

// Visitor which searches for handles closed by an event (it's the case of messages sent to
// another process which contain handles).
class CloseHandleVisitor : public fidl_codec::Visitor {
 public:
  explicit CloseHandleVisitor(const OutputEvent* output_event) : output_event_(output_event) {}

 private:
  void VisitHandleValue(const fidl_codec::HandleValue* node,
                        const fidl_codec::Type* for_type) override;

  const OutputEvent* const output_event_;
};

// Visitor which searches for handles created by an event (it's the case of messages received from
// another process which contain handles).
class CreateHandleVisitor : public fidl_codec::Visitor {
 public:
  explicit CreateHandleVisitor(const OutputEvent* output_event) : output_event_(output_event) {}

 private:
  void VisitHandleValue(const fidl_codec::HandleValue* node,
                        const fidl_codec::Type* for_type) override;

  const OutputEvent* const output_event_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_STATISTICS_H_
