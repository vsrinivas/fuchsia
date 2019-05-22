// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>
#include <stdlib.h>

#include "src/developer/bugreport/bug_reporter.h"

int main(int argc, char** argv) {
  auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();

  return fuchsia::bugreport::MakeBugReport(environment_services) ? EXIT_SUCCESS
                                                                 : EXIT_FAILURE;
}
