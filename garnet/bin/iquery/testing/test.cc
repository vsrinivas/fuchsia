// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <regex>

#include "gmock/gmock.h"

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"
#include "src/lib/files/path.h"

namespace {

constexpr char kGoldenPath[] = "/pkg/data/iquery_goldens";
constexpr char kBinPrefix[] = "/pkg/bin";

using sys::testing::EnclosingEnvironment;
using ::testing::StartsWith;

class IqueryGoldenTest : public sys::testing::TestWithEnvironment,
                         public testing::WithParamInterface<std::string> {
 protected:
  IqueryGoldenTest() {
    // Create a new enclosing environment and create the example component in
    // it.
    environment_ = CreateNewEnclosingEnvironment("test", CreateServices());
    WaitForEnclosingEnvToStart(environment_.get());
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url =
        "fuchsia-pkg://fuchsia.com/iquery_golden_test#meta/"
        "iquery_example_component.cmx";
    launch_info.arguments.push_back("--rows=5");
    launch_info.arguments.push_back("--columns=3");
    controller_ = environment_->CreateComponent(std::move(launch_info));
    // Wait until the component's output directory shows up, and save the path
    // to it.
    bool ready = false;
    controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });

    SetUpHubPath();
  }

  void SetUpHubPath() {
    auto glob = GetGlob("/hub/r/test/*/c/iquery_example_component.cmx/*/out");
    ASSERT_EQ(1u, glob.size());
    hub_directory_path_ = glob[0];
  }

  std::vector<std::string> GetGlob(const std::string& path) {
    files::Glob glob(path);
    return std::vector<std::string>{glob.begin(), glob.end()};
  }

  // Format the output with visible delimiters so we can easily copy and paste
  // to update goldens.
  std::string CopyableOutput(const std::string& output) {
    return fxl::Substitute(
        "\n======= COPYABLE OUTPUT =======\n$0\n======= END COPYABLE OUTPUT "
        "=======\n",
        output);
  }

  void RunTestCase(const std::string& filepath) {
    std::string golden_file;
    ASSERT_TRUE(files::ReadFileToString(filepath, &golden_file))
        << "Failed to open " << filepath;

    auto golden_lines = fxl::SplitStringCopy(
        golden_file, "\n", fxl::kKeepWhitespace, fxl::kSplitWantAll);
    ASSERT_GE(golden_lines.size(), 1u);

    std::string command_line;

    auto golden_it = golden_lines.begin();
    int line = 1;

    // Skip leading comment lines.
    while (golden_it->find("#") == 0) {
      ++golden_it;
      ++line;
    }

    // Get the command line and put iterator to the first comparison line.
    command_line = *golden_it++;
    ++line;
    ASSERT_THAT(command_line, StartsWith("iquery "))
        << "We only support testing iquery goldens right now.";

    // Create a temporary output file, and prepare it to become STDOUT of the
    // new process. Use the same stderr and stdin as this process so error
    // output goes to the terminal.
    std::FILE* outf = std::tmpfile();
    int out_fd = fileno(outf);
    fdio_spawn_action_t actions[] = {
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = STDIN_FILENO, .target_fd = STDIN_FILENO}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = dup(out_fd), .target_fd = STDOUT_FILENO}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}},
    };

    // Run:
    // iquery --dir=<hub> <args>
    auto args = fxl::SplitStringCopy(command_line, " ", fxl::kKeepWhitespace,
                                     fxl::kSplitWantAll);
    args.insert(args.begin() + 1,
                fxl::Substitute("--dir=$0", hub_directory_path_));
    auto command = fxl::Substitute("$0/$1", kBinPrefix, args[0]);

    const char* argv[args.size() + 1];
    argv[args.size()] = nullptr;
    for (size_t i = 0; i < args.size(); i++) {
      argv[i] = args[i].c_str();
    }

    zx_handle_t proc = ZX_HANDLE_INVALID;
    auto status =
        fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, command.c_str(),
                       argv, NULL, 2, actions, &proc, nullptr);
    ASSERT_EQ(status, ZX_OK);

    zx_signals_t observed{};
    zx_object_wait_one(proc, ZX_PROCESS_TERMINATED,
                       zx::deadline_after(zx::sec(10)).get(), &observed);
    ASSERT_TRUE(observed & ZX_PROCESS_TERMINATED);

    std::string output;
    ASSERT_TRUE(files::ReadFileDescriptorToString(out_fd, &output));

    // Replace path components containing ids with "/*/" so the test does not
    // need to know specific process or realm ids.
    std::regex match_ids("\\/\\d+\\/");
    output = std::regex_replace(output, match_ids, "/*/");

    auto output_lines = fxl::SplitStringCopy(output, "\n", fxl::kKeepWhitespace,
                                             fxl::kSplitWantAll);

    // Compare golden with output line by line.
    auto output_it = output_lines.begin();
    while (golden_it != golden_lines.end() && output_it != output_lines.end()) {
      ASSERT_EQ(std::string(output_it->data()), std::string(golden_it->data()))
          << CopyableOutput(output)
          << fxl::StringPrintf(
                 "%s:%d First difference:\nINPUT : %s\nGOLDEN: %s",
                 filepath.c_str(), line, output_it->c_str(),
                 golden_it->c_str());
      ++output_it;
      ++golden_it;
      ++line;
    }

    // Make sure both files are at the end.
    // Allow for the golden file to end with a blank line.
    ASSERT_TRUE(golden_it == golden_lines.end() ||
                *golden_it == "" && ++golden_it == golden_lines.end())
        << CopyableOutput(output)
        << "Golden file had extra lines starting at line " << line << "\n"
        << *golden_it;
    ASSERT_EQ(output_it, output_lines.end())
        << CopyableOutput(output) << "Output had extra lines starting at line "
        << line << "\n"
        << *output_it;
  }

 private:
  std::string hub_directory_path_;
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

std::vector<std::string> GetGoldenFiles() {
  std::vector<std::string> ret;
  for (const auto& filename :
       files::Glob(fxl::Substitute("$0/*", kGoldenPath))) {
    ret.push_back(filename);
  }
  return ret;
}

TEST_P(IqueryGoldenTest, MatchesGolden) { RunTestCase(GetParam()); }

// Nicely format a parameter name (file path) as a camel-case name. Stripping
// all non-alphanumeric characters, path prefix, and extension.
// Example: /data/pkg/my-file-name10.txt -> "MyFileName10".
std::string OutputTestName(const ::testing::TestParamInfo<std::string>& info) {
  auto param = files::GetBaseName(info.param);
  std::string out;

  bool cap = true;
  for (char c : param) {
    if (c == '.') {
      break;
    }
    if (!isalnum(c)) {
      cap = true;
    } else {
      if (cap) {
        c = toupper(c);
        cap = false;
      }
      out.append(1, c);
    }
  }
  return out;
};

INSTANTIATE_TEST_SUITE_P(AllFiles, IqueryGoldenTest,
                         ::testing::ValuesIn(GetGoldenFiles()), OutputTestName);

}  // namespace
