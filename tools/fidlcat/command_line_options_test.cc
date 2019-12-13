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

namespace fidlcat {

class CommandLineOptionsTest : public ::testing::Test {
 protected:
  void SetUp() override { mkdtemp(const_cast<char*>(dir_.c_str())); }

  void TearDown() override {
    for (auto& file : files_) {
      std::string full_filename = dir_ + std::filesystem::path::preferred_separator + file;
      remove(full_filename.c_str());
    }
    remove(dir_.c_str());
  }

  const std::string& dir() const { return dir_; }
  std::vector<std::string>& files() { return files_; }

 private:
  std::string dir_ = "tmp.XXXXXX";
  std::vector<std::string> files_;
};

// Test to ensure @argfile support works.
TEST_F(CommandLineOptionsTest, ArgfileTest) {
  std::ofstream os;
  std::string argfilename = "out.txt";
  std::string filename = dir() + std::filesystem::path::preferred_separator + argfilename;
  files() = {"foo.fidl.json", "bar.fidl.json"};

  // Write some content to each file, so that we have something to read to see
  // if the fidl paths were returned correctly.
  for (auto& file : files()) {
    std::ofstream fout;
    fout.open(dir() + std::filesystem::path::preferred_separator + file,
              std::ofstream::out | std::ofstream::app);
    fout << file;
    fout.close();
  }

  // Write the filenames to the argfile.
  os.open(filename, std::ofstream::out | std::ofstream::app);
  for (auto& file : files()) {
    os << file << "\n";
  }
  os.close();

  // Parse the command line.
  std::string param = "@" + filename;
  std::vector<const char*> argv = {"fakebinary", "--remote-pid", "3141", "--fidl-ir-path",
                                   param.c_str()};
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto error = ParseCommandLine(argv.size(), argv.data(), &options, &decode_options,
                                &display_options, &params);
  ASSERT_TRUE(error.empty());
  ASSERT_EQ(0U, params.size()) << "Expected 0 params, got (at least) " << params[0];

  // Expand the FIDL paths.
  std::vector<std::unique_ptr<std::istream>> paths;
  std::vector<std::string> bad_paths;
  ExpandFidlPathsFromOptions(options.fidl_ir_paths, paths, bad_paths);

  ASSERT_EQ(files().size(), paths.size());

  for (size_t i = 0; i < paths.size(); i++) {
    std::string file_contents{std::istreambuf_iterator<char>(*paths[i]), {}};
    ASSERT_TRUE(paths[i]->good());
    ASSERT_EQ(files()[i], file_contents);
  }
  ASSERT_EQ(0U, bad_paths.size());

  // files_ also acts as the list of names files to delete.
  files().push_back(argfilename);
}

// Test to ensure that non-existent files are reported accordingly.
TEST_F(CommandLineOptionsTest, BadOptionsTest) {
  // Parse the command line.
  std::vector<const char*> argv = {"fakebinary", "--fidl-ir-path", "blah.fidl.json", "--remote-pid",
                                   "3141",       "--fidl-ir-path", "@all_files.txt"};
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto error = ParseCommandLine(argv.size(), argv.data(), &options, &decode_options,
                                &display_options, &params);
  ASSERT_TRUE(error.empty());
  ASSERT_EQ(0U, params.size()) << "Expected 0 params, got (at least) " << params[0];

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
  std::string symbol_repo_path = "path/to/debug/symbols/repo";
  std::string symbol_cache = "~";
  std::string symbol_server = "gs://fuchsia-infra-debug-symbols";
  std::string remote_pid = "3141";
  std::string connect = "localhost:8080";
  std::vector<const char*> argv = {"fakebinary",
                                   "--fidl-ir-path",
                                   fidl_ir_path.c_str(),
                                   "-s",
                                   symbol_path.c_str(),
                                   "--symbol-repo-path",
                                   symbol_repo_path.c_str(),
                                   "--symbol-cache",
                                   symbol_cache.c_str(),
                                   "--symbol-server",
                                   symbol_server.c_str(),
                                   "--connect",
                                   connect.c_str(),
                                   "--remote-pid",
                                   remote_pid.c_str(),
                                   "--stack",
                                   "2",
                                   "--syscalls",
                                   "zx_handle_*",
                                   "--syscalls",
                                   "zx_channel_*",
                                   "--exclude-syscalls",
                                   "zx_handle_close",
                                   "--verbose",
                                   "error",
                                   "leftover",
                                   "args"};
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto error = ParseCommandLine(argv.size(), argv.data(), &options, &decode_options,
                                &display_options, &params);
  ASSERT_TRUE(error.empty());
  ASSERT_EQ(2U, params.size()) << "Expected 0 params, got (at least) " << params[0];
  ASSERT_EQ(connect, *options.connect);
  ASSERT_EQ(remote_pid, options.remote_pid[0]);
  ASSERT_EQ(symbol_path, options.symbol_paths[0]);
  ASSERT_EQ(symbol_repo_path, options.symbol_repo_paths[0]);
  ASSERT_EQ(symbol_cache, options.symbol_cache_path);
  ASSERT_EQ(symbol_server, options.symbol_servers[0]);
  ASSERT_EQ(fidl_ir_path, options.fidl_ir_paths[0]);
  ASSERT_EQ(2, options.stack_level);

  ASSERT_EQ(2U, options.syscall_filters.size());
  ASSERT_EQ("zx_handle_*", options.syscall_filters[0]);
  ASSERT_EQ("zx_channel_*", options.syscall_filters[1]);
  ASSERT_EQ(2U, decode_options.syscall_filters.size());

  ASSERT_EQ(1U, options.exclude_syscall_filters.size());
  ASSERT_EQ("zx_handle_close", options.exclude_syscall_filters[0]);
  ASSERT_EQ(1U, decode_options.exclude_syscall_filters.size());

  ASSERT_TRUE(fxl::ShouldCreateLogMessage(fxl::LOG_ERROR));
  ASSERT_FALSE(fxl::ShouldCreateLogMessage(fxl::LOG_INFO));
  ASSERT_TRUE(std::find(params.begin(), params.end(), "leftover") != params.end());
  ASSERT_TRUE(std::find(params.begin(), params.end(), "args") != params.end());
}

TEST_F(CommandLineOptionsTest, CantHavePidAndFilter) {
  std::string remote_pid = "3141";
  std::string filter = "echo_client";
  std::vector<const char*> argv = {"fakebinary",   "--remote_name",    filter.c_str(),
                                   "--remote-pid", remote_pid.c_str(), "leftover",
                                   "args"};
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto error = ParseCommandLine(argv.size(), argv.data(), &options, &decode_options,
                                &display_options, &params);
  ASSERT_TRUE(!error.empty());
}

// Test to ensure that help is printed when no action is requested
TEST_F(CommandLineOptionsTest, NoActionMeansFailure) {
  std::string fidl_ir_path = "blah.fidl.json";
  std::string symbol_path = "path/to/debug/symbols";
  std::string connect = "localhost:8080";
  std::vector<const char*> argv = {
      "fakebinary", "--fidl-ir-path", fidl_ir_path.c_str(), "-s",  symbol_path.c_str(),
      "--connect",  connect.c_str(),  "leftover",           "args"};
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto error = ParseCommandLine(argv.size(), argv.data(), &options, &decode_options,
                                &display_options, &params);
  ASSERT_TRUE(!error.empty());
}

TEST_F(CommandLineOptionsTest, QuietTrumpsVerbose) {
  std::string fidl_ir_path = "blah.fidl.json";
  std::string symbol_path = "path/to/debug/symbols";
  std::string remote_pid = "3141";
  std::string connect = "localhost:8080";
  std::vector<const char*> argv = {"fakebinary",
                                   "--fidl-ir-path",
                                   fidl_ir_path.c_str(),
                                   "-s",
                                   symbol_path.c_str(),
                                   "--connect",
                                   connect.c_str(),
                                   "--remote-pid",
                                   remote_pid.c_str(),
                                   "--verbose",
                                   "info",
                                   "--quiet",
                                   "2",
                                   "leftover",
                                   "args"};
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  auto error = ParseCommandLine(argv.size(), argv.data(), &options, &decode_options,
                                &display_options, &params);
  ASSERT_TRUE(fxl::ShouldCreateLogMessage(fxl::LOG_ERROR));
  ASSERT_FALSE(fxl::ShouldCreateLogMessage(fxl::LOG_INFO));
}

}  // namespace fidlcat
