// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>

#include <cstdlib>

#include "src/developer/forensics/bugreport/bug_reporter.h"

int main(int argc, char** argv) {
  return ::forensics::bugreport::MakeBugReport(sys::ServiceDirectory::CreateFromNamespace())
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
