// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_line_options.h"

#include <stdlib.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"

namespace shell {

// Test to ensure that ParseCommandLine works.
TEST(CommandLineOptions, SimpleParseCommandLineTest) {
  std::string fidl_ir_path = "blah.fidl.json";
  std::string boot_js_lib_path = "path/to/js/lib";
  std::string command_line = "\"Once upon a midnight dreary\"";

  std::vector<const char*> argv = {"fakebinary", "--fidl-ir-path",     fidl_ir_path.c_str(),
                                   "-l",         "--boot-js-lib-path", boot_js_lib_path.c_str(),
                                   "-c",         command_line.c_str(), "leftover",
                                   "args"};
  CommandLineOptions options;
  std::vector<std::string> params;
  auto status = ParseCommandLine(argv.size(), argv.data(), &options, &params);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(2U, params.size()) << "Expected 0 params, got (at least) " << params[0];
  ASSERT_EQ(fidl_ir_path, options.fidl_ir_path[0]);
  ASSERT_EQ(boot_js_lib_path, options.boot_js_lib_path[0]);
  ASSERT_EQ(command_line, options.command_string);

  ASSERT_TRUE(std::find(params.begin(), params.end(), "leftover") != params.end());
  ASSERT_TRUE(std::find(params.begin(), params.end(), "args") != params.end());
}

}  // namespace shell
