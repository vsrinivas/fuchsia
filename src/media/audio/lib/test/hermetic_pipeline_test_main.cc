// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>

#include <gtest/gtest.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/lib/test/hermetic_pipeline_test.h"

namespace {

// If a binary name was specified, set it as the syslog tag (after stripping any prepended dirs).
void SetSyslogTag(fxl::CommandLine cmdline) {
  if (cmdline.has_argv0()) {
    auto tag = cmdline.argv0();
    auto last_separator_index = tag.rfind('/');
    if (last_separator_index < tag.npos) {
      tag = tag.substr(last_separator_index + 1);
    }
    syslog::SetTags({tag});
  }
}

}  // namespace

int main(int argc, char** argv) {
  auto cmdline = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetTestSettings(cmdline)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  SetSyslogTag(cmdline);

  media::audio::test::HermeticPipelineTest::save_input_and_output_files_ =
      cmdline.HasOption("save-inputs-and-outputs");

  auto result = RUN_ALL_TESTS();

  return result;
}
