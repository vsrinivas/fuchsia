// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "src/sys/component_manager/tests/structured_config/minimal_shards/cpp_elf/receiver_config.h"

int main(int argc, const char** argv) {
  std::cout << receiver_config::Config::TakeFromStartupHandle().ToString() << std::endl;
}
