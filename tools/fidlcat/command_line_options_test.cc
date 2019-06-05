// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_line_options.h"

#include <stdlib.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace fidlcat {

class CommandLineOptionsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* tmpl = "tmp.XXXXXX";
    dir_.reset(new char[strlen(tmpl) + 1]);
    strcpy(dir_.get(), tmpl);
    mkdtemp(dir_.get());
  }

  void TearDown() override {
    for (auto& file : files_) {
      std::string full_filename = std::string(dir_.get()) +
                                  std::filesystem::path::preferred_separator +
                                  file;
      remove(full_filename.c_str());
    }
    remove(dir_.get());
  }

  std::unique_ptr<char[]> dir_;
  std::vector<std::string> files_;
};

// Test to ensure @argfile support works.
TEST_F(CommandLineOptionsTest, ArgfileTest) {
  std::ofstream os;
  std::string dir = std::string(dir_.get());
  std::string argfilename = "out.txt";
  std::string filename =
      dir + std::filesystem::path::preferred_separator + argfilename;
  files_ = {"foo.fidl.json", "bar.fidl.json"};

  // Write some content to each file, so that we have something to read to see
  // if the fidl paths were returned correctly.
  for (auto& file : files_) {
    std::ofstream fout;
    fout.open(dir + std::filesystem::path::preferred_separator + file,
              std::ofstream::out | std::ofstream::app);
    fout << file;
    fout.close();
  }

  // Write the filenames to the argfile.
  os.open(filename, std::ofstream::out | std::ofstream::app);
  for (auto& file : files_) {
    os << file << "\n";
  }
  os.close();

  // Parse the command line.
  std::string param = "@" + filename;
  const char* argv[] = {"fakebinary", "--fidl-ir-path", param.c_str()};
  int argc = sizeof(argv) / sizeof(argv[0]);
  CommandLineOptions options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto status =
      ParseCommandLine(argc, argv, &options, &display_options, &params);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(0U, params.size())
      << "Expected 0 params, got (at least) " << params[0];

  // Expand the FIDL paths.
  std::vector<std::unique_ptr<std::istream>> paths;
  std::vector<std::string> bad_paths;
  ExpandFidlPathsFromOptions(options.fidl_ir_paths, paths, bad_paths);

  ASSERT_EQ(files_.size(), paths.size());

  for (size_t i = 0; i < paths.size(); i++) {
    std::string file_contents{std::istreambuf_iterator<char>(*paths[i]), {}};
    ASSERT_TRUE(paths[i]->good());
    ASSERT_EQ(files_[i], file_contents);
  }
  ASSERT_EQ(0U, bad_paths.size());

  // files_ also acts as the list of names files to delete.
  files_.push_back(argfilename);
}

// Test to ensure that non-existent files are reported accordingly.
TEST_F(CommandLineOptionsTest, BadOptionsTest) {
  // Parse the command line.
  const char* argv[] = {"fakebinary", "--fidl-ir-path", "blah.fidl.json",
                        "--fidl-ir-path", "@all_files.txt"};
  int argc = sizeof(argv) / sizeof(argv[0]);
  CommandLineOptions options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto status =
      ParseCommandLine(argc, argv, &options, &display_options, &params);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(0U, params.size())
      << "Expected 0 params, got (at least) " << params[0];

  std::vector<std::unique_ptr<std::istream>> paths;
  std::vector<std::string> bad_paths;
  ExpandFidlPathsFromOptions(options.fidl_ir_paths, paths, bad_paths);

  ASSERT_EQ(2U, bad_paths.size());
  ASSERT_EQ(0U, paths.size());
}

// Test to ensure that ParseCommandLine works.
TEST_F(CommandLineOptionsTest, SimpleParseCommandLineTest) {
  std::string fidl_ir_path = "blah.fidl.json";
  std::string symbol_path = "path/to/debug/symbols";
  std::string remote_pid = "3141";
  std::string connect = "localhost:8080";
  const char* argv[] = {"fakebinary",
                        "--fidl-ir-path",
                        fidl_ir_path.c_str(),
                        "-s",
                        symbol_path.c_str(),
                        "--connect",
                        connect.c_str(),
                        "--remote-pid",
                        remote_pid.c_str(),
                        "leftover",
                        "args"};
  int argc = sizeof(argv) / sizeof(argv[0]);
  CommandLineOptions options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto status =
      ParseCommandLine(argc, argv, &options, &display_options, &params);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(2U, params.size())
      << "Expected 0 params, got (at least) " << params[0];
  ASSERT_EQ(connect, *options.connect);
  ASSERT_EQ(remote_pid, *options.remote_pid);
  ASSERT_EQ(symbol_path, options.symbol_paths[0]);
  ASSERT_EQ(fidl_ir_path, options.fidl_ir_paths[0]);
  ASSERT_TRUE(std::find(params.begin(), params.end(), "leftover") !=
              params.end());
  ASSERT_TRUE(std::find(params.begin(), params.end(), "args") != params.end());
}

TEST_F(CommandLineOptionsTest, CantHavePidAndFilter) {
  std::string remote_pid = "3141";
  std::string filter = "echo_client";
  const char* argv[] = {"fakebinary",   "--filter",         filter.c_str(),
                        "--remote-pid", remote_pid.c_str(), "leftover",
                        "args"};
  int argc = sizeof(argv) / sizeof(argv[0]);
  CommandLineOptions options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto status =
      ParseCommandLine(argc, argv, &options, &display_options, &params);
  ASSERT_TRUE(!status.ok());
}

}  // namespace fidlcat
