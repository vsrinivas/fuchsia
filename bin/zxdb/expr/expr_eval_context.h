// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Err;
class ExprValue;

// Interface used by expression evaluation to communicate with the outside
// world.
class ExprEvalContext : public fxl::RefCountedThreadSafe<ExprEvalContext> {
 public:
  virtual ~ExprEvalContext() = default;

  // Issues the callback with the value of the given variable in the context of
  // the current expression evaluation.
  //
  // The callback may be issued asynchronously in the future if communication
  // with the remote debugged application is required. The callback may be
  // issued reentrantly for synchronously available data.
  virtual void GetVariable(const std::string& name,
                            std::function<void(const Err& err, ExprValue value)>) = 0;
};

}  // namespace zxdb
