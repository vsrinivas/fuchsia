// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/step_base.h"

#include "fidl/flat_ast.h"

namespace fidl::flat {

StepBase::StepBase(Library* library) : ReporterMixin(library->reporter()), library_(library) {}

}  // namespace fidl::flat
