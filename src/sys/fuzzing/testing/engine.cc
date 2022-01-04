// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/testing/runner.h"

int main() {
  fuzzing::ControllerProviderImpl provider;
  return provider.Run(std::make_unique<fuzzing::SimpleFixedRunner>());
}
