// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/test/test_settings.h"

namespace media::audio::test {
extern bool flag_save_inputs_and_outputs;
}  // namespace media::audio::test

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  media::audio::test::flag_save_inputs_and_outputs = cl.HasOption("save-inputs-and-outputs");
  return RUN_ALL_TESTS();
}
