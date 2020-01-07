// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cmdline/args_parser.h>
#include <stdio.h>

#include "tools/kazoo/outputs.h"
#include "tools/kazoo/string_util.h"
#include "tools/kazoo/syscall_library.h"

namespace {

struct CommandLineOptions {
  std::optional<std::string> arm64_asm;
  std::optional<std::string> category;
  std::optional<std::string> go_syscall_arm64_asm;
  std::optional<std::string> go_syscall_stubs;
  std::optional<std::string> go_syscall_x86_asm;
  std::optional<std::string> go_vdso_arm64_calls;
  std::optional<std::string> go_vdso_keys;
  std::optional<std::string> go_vdso_x86_calls;
  std::optional<std::string> json;
  std::optional<std::string> kernel_branches;
  std::optional<std::string> kernel_header;
  std::optional<std::string> kernel_wrappers;
  std::optional<std::string> ktrace;
  std::optional<std::string> rust;
  std::optional<std::string> syscall_numbers;
  std::optional<std::string> user_header;
  std::optional<std::string> vdso_header;
  std::optional<std::string> vdso_wrappers;
  std::optional<std::string> x86_asm;
};

constexpr const char kHelpIntro[] = R"(kazoo [ <options> ] <fidlc-ir.json>

  kazoo converts from fidlc's json IR representation of syscalls to a variety
  output formats used by the kernel and userspace.

Options:

)";

constexpr const char kArm64AsmHelp[] = R"(  --arm64-asm=FILENAME
    The output name for the .S file ARM64 syscalls.)";

constexpr const char kCategoryHelp[] = R"(  --category=FILENAME
    The output name for the .inc categories file.)";

constexpr const char kGoSyscallArm64AsmHelp[] = R"(  --go-syscall-arm64-asm=FILENAME
    The output name for the Go syscall/zx arm .s file.)";

constexpr const char kGoSyscallStubsHelp[] = R"(  --go-syscall-stubs=FILENAME
    The output name for the Go syscall/zx stubs .go file.)";

constexpr const char kGoSyscallX86AsmHelp[] = R"(  --go-syscall-x86-asm=FILENAME
    The output name for the Go syscall/zx x86 .s file.)";

constexpr const char kGoVdsoKeysHelp[] = R"(  --go-vdso-keys=FILENAME
    The output name for the Go runtime VDSO keys file.)";

constexpr const char kGoVdsoArm64CallsHelp[] = R"(  --go-vdso-arm64-calls=FILENAME
    The output name for the Go runtime ARM VDSO calls file.)";

constexpr const char kGoVdsoX86CallsHelp[] = R"(  --go-vdso-x86-calls=FILENAME
    The output name for the Go runtime x86-64 VDSO calls file.)";

constexpr const char kJsonHelp[] = R"(  --json=FILENAME
    The output name for the .json syscall definitions.)";

constexpr const char kKernelBranchesHelp[] = R"(  --kernel-branches=FILENAME
    The output name for the .S file used for kernel syscall dispatch.)";

constexpr const char kKernelHeaderHelp[] = R"(  --kernel-header=FILENAME
    The output name for the .h file used for kernel header.)";

constexpr const char kKernelWrappersHelp[] = R"(  --kernel-wrappers=FILENAME
    The output name for the .inc file used for kernel wrappers.)";

constexpr const char kKtraceHelp[] = R"(  --ktrace=FILENAME
    The output name for the .inc file used for kernel tracing.)";

constexpr const char kRustHelp[] = R"(  --rust=FILENAME
    The output name for the .rs file used for Rust syscall definitions.)";

constexpr const char kSyscallNumbersHelp[] = R"(  --syscall-numbers=FILENAME
    The output name for the .h file used for syscall numbers.)";

constexpr const char kUserHeaderHelp[] = R"(  --user-header=FILENAME
    The output name for the .h file used for the user syscall header.)";

constexpr const char kVdsoHeaderHelp[] = R"(  --vdso-header=FILENAME
    The output name for the .h file used for VDSO prototypes.)";

constexpr const char kVdsoWrappersHelp[] = R"(  --vdso-wrappers=FILENAME
    The output name for the .inc file used for blocking VDSO call wrappers.)";

constexpr const char kX86AsmHelp[] = R"(  --x86-asm=FILENAME
    The output name for the .S file x86 syscalls.)";

const char kHelpHelp[] = R"(  --help
  -h
    Prints all command line switches.)";

cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;
  parser.AddSwitch("arm64-asm", 0, kArm64AsmHelp, &CommandLineOptions::arm64_asm);
  parser.AddSwitch("category", 0, kCategoryHelp, &CommandLineOptions::category);
  parser.AddSwitch("go-syscall-arm64-asm", 0, kGoSyscallArm64AsmHelp,
                   &CommandLineOptions::go_syscall_arm64_asm);
  parser.AddSwitch("go-syscall-stubs", 0, kGoSyscallStubsHelp,
                   &CommandLineOptions::go_syscall_stubs);
  parser.AddSwitch("go-syscall-x86-asm", 0, kGoSyscallX86AsmHelp,
                   &CommandLineOptions::go_syscall_x86_asm);
  parser.AddSwitch("go-vdso-arm64-calls", 0, kGoVdsoArm64CallsHelp,
                   &CommandLineOptions::go_vdso_arm64_calls);
  parser.AddSwitch("go-vdso-keys", 0, kGoVdsoKeysHelp, &CommandLineOptions::go_vdso_keys);
  parser.AddSwitch("go-vdso-x86-calls", 0, kGoVdsoX86CallsHelp,
                   &CommandLineOptions::go_vdso_x86_calls);
  parser.AddSwitch("json", 0, kJsonHelp, &CommandLineOptions::json);
  parser.AddSwitch("kernel-branches", 0, kKernelBranchesHelp, &CommandLineOptions::kernel_branches);
  parser.AddSwitch("kernel-header", 0, kKernelHeaderHelp, &CommandLineOptions::kernel_header);
  parser.AddSwitch("kernel-wrappers", 0, kKernelWrappersHelp, &CommandLineOptions::kernel_wrappers);
  parser.AddSwitch("ktrace", 0, kKtraceHelp, &CommandLineOptions::ktrace);
  parser.AddSwitch("rust", 0, kRustHelp, &CommandLineOptions::rust);
  parser.AddSwitch("syscall-numbers", 0, kSyscallNumbersHelp, &CommandLineOptions::syscall_numbers);
  parser.AddSwitch("user-header", 0, kUserHeaderHelp, &CommandLineOptions::user_header);
  parser.AddSwitch("vdso-header", 0, kVdsoHeaderHelp, &CommandLineOptions::vdso_header);
  parser.AddSwitch("vdso-wrappers", 0, kVdsoWrappersHelp, &CommandLineOptions::vdso_wrappers);
  parser.AddSwitch("x86-asm", 0, kX86AsmHelp, &CommandLineOptions::x86_asm);
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
  if (!ReadFileToString(params[0], &contents)) {
    fprintf(stderr, "Couldn't read %s.\n", params[0].c_str());
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
      {&options.arm64_asm, AsmOutput},
      {&options.category, CategoryOutput},
      {&options.go_syscall_arm64_asm, GoSyscallsAsm},
      {&options.go_syscall_stubs, GoSyscallsStubs},
      {&options.go_syscall_x86_asm, GoSyscallsAsm},
      {&options.go_vdso_arm64_calls, GoVdsoArm64Calls},
      {&options.go_vdso_keys, GoVdsoKeys},
      {&options.go_vdso_x86_calls, GoVdsoX86Calls},
      {&options.json, JsonOutput},
      {&options.kernel_branches, KernelBranchesOutput},
      {&options.kernel_header, KernelHeaderOutput},
      {&options.kernel_wrappers, KernelWrappersOutput},
      {&options.ktrace, KtraceOutput},
      {&options.rust, RustOutput},
      {&options.syscall_numbers, SyscallNumbersOutput},
      {&options.user_header, UserHeaderOutput},
      {&options.vdso_header, VdsoHeaderOutput},
      {&options.vdso_wrappers, VdsoWrappersOutput},
      {&options.x86_asm, AsmOutput},
  };

  for (const auto& backend : backends) {
    if (*backend.name) {
      FileWriter writer;
      if (!writer.Open(**backend.name) || !backend.output(library, &writer)) {
        return 1;
      }
      ++output_count;
    }
  }

  if (output_count == 0) {
    fprintf(stderr, "No output types selected.\n");
    return 1;
  }

  return 0;
}
