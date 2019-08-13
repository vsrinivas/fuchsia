// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <cmdline/args_parser.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"
#include "tools/kazoo/outputs.h"
#include "tools/kazoo/syscall_library.h"

namespace {

struct CommandLineOptions {
  std::optional<std::string> kernel_branches;
  std::optional<std::string> ktrace;
  std::optional<std::string> syscall_numbers;
};

constexpr const char kHelpIntro[] = R"(kazoo [ <options> ] <fidlc-ir.json>

  kazoo converts from fidlc's json IR representation of syscalls to a variety
  output formats used by the kernel and userspace.

Options:

)";

constexpr const char kKernelBranchesHelp[] = R"(  --kernel-branches=FILENAME
    The output name for the .S file used for kernel syscall dispatch.)";

constexpr const char kKtraceHelp[] = R"(  --ktrace=FILENAME
    The output name for the .inc file used for kernel tracing.)";

constexpr const char kSyscallNumbersHelp[] = R"(  --syscall-numbers=FILENAME
    The output name for the .h file used for syscall numbers.)";

const char kHelpHelp[] = R"(  --help
  -h
    Prints all command line switches.)";

cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;
  parser.AddSwitch("kernel-branches", 0, kKernelBranchesHelp, &CommandLineOptions::kernel_branches);
  parser.AddSwitch("ktrace", 0, kKtraceHelp, &CommandLineOptions::ktrace);
  parser.AddSwitch("syscall-numbers", 0, kSyscallNumbersHelp, &CommandLineOptions::syscall_numbers);
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error()) {
    return status;
  }

  if (requested_help || params->size() != 1) {
    return cmdline::Status::Error(kHelpIntro + parser.GetHelp());
  }

  return cmdline::Status::Ok();
}

}  // namespace

int main(int argc, const char* argv[]) {
  CommandLineOptions options;
  std::vector<std::string> params;
  cmdline::Status status = ParseCommandLine(argc, argv, &options, &params);
  if (status.has_error()) {
    puts(status.error_message().c_str());
    return 1;
  }

  std::string contents;
  if (!files::ReadFileToString(params[0], &contents)) {
    FXL_LOG(ERROR) << "Couldn't read " << params[0] << ".";
    return 1;
  }

  SyscallLibrary library;
  if (!SyscallLibraryLoader::FromJson(contents, &library, /*match_original_order=*/true)) {
    return 1;
  }

  int output_count = 0;

  struct {
    std::optional<std::string>* name;
    bool (*output)(const SyscallLibrary&, Writer*);
  } backends[] = {
      {&options.kernel_branches, KernelBranchesOutput},
      {&options.ktrace, KtraceOutput},
      {&options.syscall_numbers, SyscallNumbersOutput},
  };

  for (const auto& backend : backends) {
    if (*backend.name) {
      FileWriter writer;
      if (!writer.Open(**backend.name) || !backend.output(library, &writer)) {
        return 1;
      }
      printf("Wrote %s\n", (**backend.name).c_str());
      ++output_count;
    }
  }

  if (output_count == 0) {
    FXL_LOG(WARNING) << "No output types selected.";
    return 1;
  }

  return 0;
}
