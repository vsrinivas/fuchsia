// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/cmdline/status.h>
#include <stdio.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fidl/findings_json.h>
#include <fidl/lexer.h>
#include <fidl/linter.h>
#include <fidl/parser.h>
#include <fidl/source_manager.h>
#include <fidl/tree_visitor.h>

#include "command_line_options.h"

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

bool Lint(const fidl::SourceFile& source_file, fidl::Findings* findings,
          fidl::ErrorReporter* error_reporter, const std::set<std::string>& included_checks,
          const std::set<std::string>& excluded_checks, bool exclude_by_default,
          std::set<std::string>* excluded_checks_not_found) {
  fidl::Lexer lexer(source_file, error_reporter);
  fidl::Parser parser(&lexer, error_reporter);
  std::unique_ptr<fidl::raw::File> ast = parser.Parse();
  if (!parser.Ok()) {
    return false;
  }

  fidl::linter::Linter linter;

  linter.set_included_checks(included_checks);
  linter.set_excluded_checks(excluded_checks);
  linter.set_exclude_by_default(exclude_by_default);

  return linter.Lint(ast, findings, excluded_checks_not_found);
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
  for (auto filepath : filepaths) {
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

  bool exclude_by_default = (options.included_checks.size() > 0) && options.excluded_checks.empty();

  // Convert command line vectors to sets, and add internally-disabled checks to excluded
  auto included_checks =
      std::set<std::string>(options.included_checks.begin(), options.included_checks.end());
  auto excluded_and_disabled_checks =
      std::set<std::string>(options.excluded_checks.begin(), options.excluded_checks.end());

  // The following checks can be opted-in via command line option "included-checks",
  // but are otherwise disabled for the reasons described in the comments:

  // The name-repeats-* checks are very noisy, and sometimes produce
  // unexpected findings. Rules are being refined, but for now, these
  // are suppressed.
  excluded_and_disabled_checks.insert("name-repeats-library-name");
  excluded_and_disabled_checks.insert("name-repeats-enclosing-type-name");

  // This check does currently highlight some potential issues with
  // formatting and with 2-slash comments that will be converted to
  // 3-slash Doc-Comments, but the rule cannot currently check 3-slash
  // Doc-Comments (they are stripped out before they reach the linter,
  // and converted to Attributes), and trailing non-Doc comments are
  // supposed to be allowed. Therefore, the rule will eventually be
  // removed, once the valid issues it currently surfaces have been
  // addressed.
  excluded_and_disabled_checks.insert("no-trailing-comment");

  fidl::Findings findings;
  fidl::ErrorReporter error_reporter;
  for (const auto& source_file : source_manager.sources()) {
    Lint(*source_file, &findings, &error_reporter, included_checks, excluded_and_disabled_checks,
         exclude_by_default, &excluded_checks_not_found);
    // Lint() returns true if no findings were added, but that information is not useful to the
    // program at this point. We return a 0 or 1 status code based on the final size of the findings
    // collection.
  }
  if (options.format == "text") {
    fidl::utils::WriteFindingsToErrorReporter(findings, &error_reporter);
    error_reporter.PrintReports();
  } else {
    assert(options.format == "json");  // should never be false
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
  std::exit(findings.size() == 0 ? 0 : 1);
}
