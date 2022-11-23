// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/cmdline/status.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tools/fidl/fidlc/include/fidl/findings.h"
#include "tools/fidl/fidlc/include/fidl/findings_json.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/linter.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/source_manager.h"
#include "tools/fidl/fidlc/include/fidl/tree_visitor.h"
#include "tools/fidl/fidlc/linter/command_line_options.h"

namespace {

[[noreturn]] void FailWithUsage(const std::string& argv0, const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  std::cerr << fidl::linter::Usage(argv0) << std::endl;
  exit(2);  // Exit code 1 is reserved to indicate lint findings
}

[[noreturn]] void Fail(const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  exit(2);  // Exit code 1 is reserved to indicate lint findings
}

fidl::Finding DiagnosticToFinding(const fidl::Diagnostic& diag) {
  const char* check_id = nullptr;
  switch (diag.get_severity()) {
    case fidl::DiagnosticKind::kError:
      check_id = "parse-error";
      break;
    case fidl::DiagnosticKind::kWarning:
      check_id = "parse-warning";
      break;
    case fidl::DiagnosticKind::kRetired:
      assert(false &&
             "this diagnostic kind must never be shown - it only reserves retired error numerals");
      break;
  }
  return fidl::Finding(diag.span, check_id, diag.Print());
}

void Lint(const fidl::SourceFile& source_file, fidl::Findings* findings,
          const std::set<std::string>& included_checks,
          const std::set<std::string>& excluded_checks, bool exclude_by_default,
          std::set<std::string>* excluded_checks_not_found) {
  fidl::Reporter reporter;
  fidl::Lexer lexer(source_file, &reporter);
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  fidl::Parser parser(&lexer, &reporter, experimental_flags);
  std::unique_ptr<fidl::raw::File> ast = parser.Parse();
  for (auto* diag : reporter.Diagnostics()) {
    findings->push_back(DiagnosticToFinding(*diag));
  }
  if (!parser.Success()) {
    return;
  }

  fidl::linter::Linter linter;

  linter.set_included_checks(included_checks);
  linter.set_excluded_checks(excluded_checks);
  linter.set_exclude_by_default(exclude_by_default);

  linter.Lint(ast, findings, excluded_checks_not_found);
}

}  // namespace

int main(int argc, char* argv[]) {
  fidl::linter::CommandLineOptions options;
  std::vector<std::string> filepaths;
  cmdline::Status status =
      fidl::linter::ParseCommandLine(argc, const_cast<const char**>(argv), &options, &filepaths);
  if (status.has_error()) {
    Fail("%s\n", status.error_message().c_str());
  }

  if (filepaths.empty()) {
    FailWithUsage(argv[0], "No files provided\n");
  }

  fidl::SourceManager source_manager;

  // Process filenames.
  for (const auto& filepath : filepaths) {
    if (!source_manager.CreateSource(filepath)) {
      Fail("Couldn't read in source data from %s\n", filepath.c_str());
    }
  }

  std::set<std::string> excluded_checks_not_found;
  if (options.must_find_excluded_checks) {
    // copy excluded checks specified in command line options, and the linter will remove each one
    // encountered during linting.
    excluded_checks_not_found =
        std::set<std::string>(options.excluded_checks.begin(), options.excluded_checks.end());
  }

  bool exclude_by_default = !options.included_checks.empty() && options.excluded_checks.empty();

  // Convert command line vectors to sets, and add internally-disabled checks to excluded
  auto included_checks =
      std::set<std::string>(options.included_checks.begin(), options.included_checks.end());
  auto excluded_checks =
      std::set<std::string>(options.excluded_checks.begin(), options.excluded_checks.end());

  // Add experimental checks to included checks. Experimental checks don't count
  // for enabling exclude_by_default, but do get added added to included_checks
  // to turn them on. Merging included-checks and experimental-checks allows
  // experimental checks to be enabled through either the --include-checks flag
  // or the --experimental-checks flag, which makes it possible to use
  // exclude-by-default mode even if you only want to turn on experimental
  // checks, by passing them through --include-checks rather than
  // --experimental-checks.
  //
  // Note that this works in reverse as well; it is possible to enable a normal
  // check via --experimental-checks, however this has no effect unless the
  // check is also being excluded via --exclude-checks or exclude-by-default in
  // being used because some other check was passed with --include-checks.
  // Allowing non-experimental checks to be enabled via --experimental-checks
  // ensures forward compatibility when a previously-experimental check is
  // officially released an so no-longer experimental.
  included_checks.insert(options.experimental_checks.begin(), options.experimental_checks.end());

  fidl::Findings findings;
  bool enable_color = !std::getenv("NO_COLOR") && isatty(fileno(stderr));
  for (const auto& source_file : source_manager.sources()) {
    Lint(*source_file, &findings, included_checks, excluded_checks, exclude_by_default,
         &excluded_checks_not_found);
  }

  if (options.format == "text") {
    auto lints = fidl::utils::FormatFindings(findings, enable_color);
    for (const auto& lint : lints) {
      fprintf(stderr, "%s\n", lint.c_str());
    }
  } else {
    ZX_ASSERT(options.format == "json");  // should never be false
    std::cout << fidl::FindingsJson(findings).Produce().str();
  }

  if (!excluded_checks_not_found.empty()) {
    std::ostringstream os;
    os << "The following checks were excluded but were never encountered:" << std::endl;
    for (auto& check_id : excluded_checks_not_found) {
      os << "  * " << check_id << std::endl;
    }
    os << "Please remove these checks from your excluded_checks list and try again." << std::endl;
    Fail(os.str().c_str());
  }

  // Exit with a status of '1' if there were any findings (at least one file was not "lint-free")
  return findings.empty() ? 0 : 1;
}
