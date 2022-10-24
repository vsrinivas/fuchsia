// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cmdline/args_parser.h>
#include <stdio.h>

#include <set>

#include "tools/kazoo/outputs.h"
#include "tools/kazoo/string_util.h"
#include "tools/kazoo/syscall_library.h"

namespace {

struct CommandLineOptions {
  std::optional<std::string> category;
  std::optional<std::string> c_ulib_header;
  std::optional<std::string> go_syscall_arm64_asm;
  std::optional<std::string> go_syscall_stubs;
  std::optional<std::string> go_syscall_x86_asm;
  std::optional<std::string> go_vdso_arm64_calls;
  std::optional<std::string> go_vdso_keys;
  std::optional<std::string> go_vdso_x86_calls;
  std::optional<std::string> kernel_header;
  std::optional<std::string> kernel_wrappers;
  std::optional<std::string> next_public_header;
  std::optional<std::string> private_header;
  std::optional<std::string> public_header;
  std::optional<std::string> rust;
  std::optional<std::string> syscall_numbers;
  std::optional<std::string> testonly_public_header;
};

constexpr const char kHelpIntro[] = R"(kazoo [ <options> ] <fidlc-ir.json>

  kazoo converts from fidlc's json IR representation of syscalls to a variety
  output formats used by the kernel and userspace.

Options:

)";

constexpr const char kCategoryHelp[] = R"(  --category=FILENAME
    The output name for the .inc categories file.)";

constexpr const char kCUlibHeaderHelp[] = R"(  --c-ulib-header=FILENAME
    The output name for the .h file used for a regular userspace library.)";

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

constexpr const char kNextPublicHeaderHelp[] = R"(  --next-public-header=FILENAME
    The output name for the .inc file used for the next public vDSO API header.)";

constexpr const char kKernelHeaderHelp[] = R"(  --kernel-header=FILENAME
    The output name for the .inc file used for kernel declarations.)";

constexpr const char kKernelWrappersHelp[] = R"(  --kernel-wrappers=FILENAME
    The output name for the .inc file used for kernel wrappers.)";

constexpr const char kPrivateHeaderHelp[] = R"(  --private-header=FILENAME
    The output name for the .inc file used for the vDSO-private header.)";

constexpr const char kPublicHeaderHelp[] = R"(  --public-header=FILENAME
    The output name for the .inc file used for the public vDSO API header.)";

constexpr const char kRustHelp[] = R"(  --rust=FILENAME
    The output name for the .rs file used for Rust syscall definitions.)";

constexpr const char kSyscallNumbersHelp[] = R"(  --syscall-numbers=FILENAME
    The output name for the .h file used for syscall numbers.)";

constexpr const char kTestonlyPublicHeaderHelp[] = R"(  --testonly-public-header=FILENAME
    The output name for the .inc file used for the testonly public vDSO API header.)";

const char kHelpHelp[] = R"(  --help
  -h
    Prints all command line switches.)";

const char kExcludeHelp[] = R"(  --exclude=someattrib
    Exclude all syscalls annotated up with [someattrib], e.g. testonly. Can be repeated.)";

cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params,
                                 std::set<std::string>* excludes) {
  cmdline::ArgsParser<CommandLineOptions> parser;
  parser.AddSwitch("category", 0, kCategoryHelp, &CommandLineOptions::category);
  parser.AddSwitch("c-ulib-header", 0, kCUlibHeaderHelp, &CommandLineOptions::c_ulib_header);
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
  parser.AddSwitch("kernel-header", 0, kKernelHeaderHelp, &CommandLineOptions::kernel_header);
  parser.AddSwitch("kernel-wrappers", 0, kKernelWrappersHelp, &CommandLineOptions::kernel_wrappers);
  parser.AddSwitch("next-public-header", 0, kNextPublicHeaderHelp,
                   &CommandLineOptions::next_public_header);
  parser.AddSwitch("private-header", 0, kPrivateHeaderHelp, &CommandLineOptions::private_header);
  parser.AddSwitch("public-header", 0, kPublicHeaderHelp, &CommandLineOptions::public_header);
  parser.AddSwitch("rust", 0, kRustHelp, &CommandLineOptions::rust);
  parser.AddSwitch("syscall-numbers", 0, kSyscallNumbersHelp, &CommandLineOptions::syscall_numbers);
  parser.AddSwitch("testonly-public-header", 0, kTestonlyPublicHeaderHelp,
                   &CommandLineOptions::testonly_public_header);

  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  excludes->clear();
  parser.AddGeneralSwitch("exclude", 0, kExcludeHelp, [&excludes](const std::string& exclude) {
    excludes->insert(exclude);
    return cmdline::Status::Ok();
  });

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
  std::set<std::string> excludes;
  cmdline::Status status = ParseCommandLine(argc, argv, &options, &params, &excludes);
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
  if (!SyscallLibraryLoader::FromJson(contents, &library)) {
    fprintf(stderr, "Unable to read fidlc JSON IR %s.\n", params[0].c_str());
    return 1;
  }

  library.FilterSyscalls(excludes);

  int output_count = 0;

  struct {
    std::optional<std::string>* name;
    bool (*output)(const SyscallLibrary&, Writer*);
  } backends[] = {
      {&options.category, CategoryOutput},
      {&options.c_ulib_header, CUlibHeaderOutput},
      {&options.next_public_header, NextPublicDeclarationsOutput},
      {&options.go_syscall_arm64_asm, GoSyscallsAsm},
      {&options.go_syscall_stubs, GoSyscallsStubs},
      {&options.go_syscall_x86_asm, GoSyscallsAsm},
      {&options.go_vdso_arm64_calls, GoVdsoArm64Calls},
      {&options.go_vdso_keys, GoVdsoKeys},
      {&options.go_vdso_x86_calls, GoVdsoX86Calls},
      {&options.kernel_header, KernelDeclarationsOutput},
      {&options.kernel_wrappers, KernelWrappersOutput},
      {&options.private_header, PrivateDeclarationsOutput},
      {&options.public_header, PublicDeclarationsOutput},
      {&options.testonly_public_header, TestonlyPublicDeclarationsOutput},
      {&options.rust, RustOutput},
      {&options.syscall_numbers, SyscallNumbersOutput},
  };

  for (const auto& backend : backends) {
    if (*backend.name) {
      Writer writer;
      if (!backend.output(library, &writer) || !WriteFileIfChanged(**backend.name, writer.Out())) {
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
