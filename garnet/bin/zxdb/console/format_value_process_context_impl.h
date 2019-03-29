// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/console/format_value.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Process;
class Target;

// Implementation of FormatValue::ProcessContext given a Process pointer. See
// also MockFormatValueProcessContext.
class FormatValueProcessContextImpl : public FormatValue::ProcessContext {
 public:
  // This will handle non-running targets by failing symbol lookup.
  explicit FormatValueProcessContextImpl(Target* target);
  explicit FormatValueProcessContextImpl(Process* process);
  ~FormatValueProcessContextImpl() override;

  // FormatValue::ProcessContext implementation.
  Location GetLocationForAddress(uint64_t address) const override;

 private:
  // The lifetime of this object will be managed by FormatValue which may be
  // independent of the Process object. Therefore this persistent Process
  // pointer must be weak.
  fxl::WeakPtr<Process> weak_process_;
};

}  // namespace zxdb
