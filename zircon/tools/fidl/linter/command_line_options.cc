// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_line_options.h"

#include <lib/cmdline/args_parser.h>

#include <iostream>

namespace fidl {
namespace linter {

namespace help {

// Appears at the top of the --help output above the switch list.
const char kArgSpec[] = "[--options] file1.fidl [fileN.fidl...]";
const char kIntro[] = R"(
Options:
)";

const char kIncludeCheck[] = R"(  --include-check=<check-id>
   -i A check ID to check, excluding all others not explicitly included. By
      default, all checks are performed, if not explicitly excluded (with
      the --exclude-check option). Multiple check IDs can be included with:
        fidl-lint -i some-check -i another-check)";
const char kExcludeCheck[] = R"(  --exclude-check=<check-id>
   -e A check ID to exclude from checking. All others will be included unless
      an --included-check option is present. --include-check overrides any
      --exclude-check. Option order is ignored. Multiple check IDs can be
      excluded with:
        fidl-lint -e some-check -e another-check)";
const char kMustFindExcludedChecks[] = R"(  --must-find-excluded-checks
   -m If this flag is set, at least one --exclude-check option is required.
      After lint checking all given FIDL files, if an excluded check is
      not encountered, output an error message and exit with an error
      status code. This can be used to temporarily excluded checks,
      resolve them over time, and once resolved, the error will force
      the developer to remove the unnecessary exclude, preventing the
      same lint error from being reintroduced in the future.)";
const char kFormat[] = R"(  --format=[text|json]
   -f Lint output format (text or json))";
const char kHelp[] = R"(  --help
   -h Print this help message.)";

}  // namespace help

std::string Usage(std::string argv0) {
  return argv0 + " " + help::kArgSpec +
         "\n(--help for more details))\n"
         "\n"
         "Returns exit status 0 if no lint issues were found, 1 if lint tests were\n"
         "successful but some lint issues were found, or 2 for all other errors.";
}

cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  std::stringstream suggestion;
  suggestion << "Try: " << argv[0] << " --help";
  if (argc == 1) {
    return cmdline::Status::Error(suggestion.str());
  }

  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("include-check", 'i', help::kIncludeCheck, &CommandLineOptions::included_checks);
  parser.AddSwitch("exclude-check", 'e', help::kExcludeCheck, &CommandLineOptions::excluded_checks);
  parser.AddSwitch("must-find-excluded-checks", 'm', help::kMustFindExcludedChecks,
                   &CommandLineOptions::must_find_excluded_checks);
  parser.AddSwitch("format", 'f', help::kFormat, &CommandLineOptions::format,
                   [](const std::string& format) -> cmdline::Status {
                     if (format == "text" || format == "json") {
                       return cmdline::Status::Ok();
                     }
                     return cmdline::Status::Error("Invalid value for --format: " + format);
                   });

  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', help::kHelp, [&requested_help]() { requested_help = true; });

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error()) {
    return status;
  }

  // Handle --help switch since we're the one that knows about the switches.
  if (requested_help) {
    return cmdline::Status::Error(Usage(argv[0]) + "\n" + help::kIntro + parser.GetHelp());
  }

  if (options->must_find_excluded_checks && options->excluded_checks.empty()) {
    return cmdline::Status::Error(
        "--must-find-excluded-checks (-m) flag is only valid if at least"
        "one check is excluded, with --exclude-check.");
  }

  if (params->size() > 0) {
    if ((*params)[0] == "printcurrentoptions") {
      std::stringstream current_options;
      for (auto check : options->included_checks) {
        current_options << "include-check: " << check << std::endl;
      }
      for (auto check : options->excluded_checks) {
        current_options << "exclude-check: " << check << std::endl;
      }
      current_options << "format: " << options->format << std::endl;
      return cmdline::Status::Error(current_options.str());
    }
  }

  return cmdline::Status::Ok();
}

}  // namespace linter
}  // namespace fidl
