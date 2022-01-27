// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_STEP_BASE_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_STEP_BASE_H_

#include "fidl/reporter.h"

namespace fidl::flat {

class Library;

// StepBase is the base class for compilation steps. Compiling a library
// consists of performing all steps in sequence. Each step succeeds (no
// additional errors) or fails (additional errors reported) as a unit, and
// typically tries to process the entire library rather than stopping after the
// first error. For certain major steps, we abort compilation if the step fails,
// meaning later steps can rely on invariants from that step succeeding. See
// Library::Compile for the logic.
class StepBase : protected ReporterMixin {
 public:
  explicit StepBase(Library* library);
  // TODO(fxbug.dev/90281): Remove this constructor. It is currently needed
  // because in types_tests.cc sometimes the library is null.
  explicit StepBase(Library* library, Reporter* reporter)
      : ReporterMixin(reporter), library_(library) {}
  StepBase(const StepBase&) = delete;

  bool Run() {
    auto checkpoint = reporter()->Checkpoint();
    RunImpl();
    return checkpoint.NoNewErrors();
  }

 protected:
  // Implementations must report errors via ReporterMixin. If no errors are
  // reported, the step is considered successful.
  virtual void RunImpl() = 0;

  Library* library_;  // link to library for which this step was created
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_STEP_BASE_H_
