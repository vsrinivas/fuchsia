// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmdline/status.h>
#include <errno.h>
#include <fidl/findings_json.h>
#include <fidl/lexer.h>
#include <fidl/linter.h>
#include <fidl/parser.h>
#include <fidl/source_manager.h>
#include <fidl/tree_visitor.h>
#include <stdio.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "command_line_options.h"

namespace {

[[noreturn]] void FailWithUsage(const std::string& argv0, const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  std::cerr << fidl::linter::Usage(argv0) << std::endl;
  exit(1);
}

[[noreturn]] void Fail(const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  exit(1);
}

bool Lint(const fidl::linter::CommandLineOptions& options, const fidl::SourceFile& source_file,
          fidl::Findings* findings, fidl::ErrorReporter* error_reporter, std::string& output) {
  fidl::Lexer lexer(source_file, error_reporter);
  fidl::Parser parser(&lexer, error_reporter);
  std::unique_ptr<fidl::raw::File> ast = parser.Parse();
  if (!parser.Ok()) {
    return false;
  }
  fidl::linter::Linter linter;

  // The following Excludes can be opted in via command line option:

  // The name-repeats-* checks are very noisy, and sometimes produce
  // unexpected findings. Rules are being refined, but for now, these
  // are suppressed.
  linter.ExcludeCheckId("name-repeats-library-name");
  linter.ExcludeCheckId("name-repeats-enclosing-type-name");

  // This check does currently highlight some potential issues with
  // formatting and with 2-slash comments that will be converted to
  // 3-slash Doc-Comments, but the rule cannot currently check 3-slash
  // Doc-Comments (they are stripped out before they reach the linter,
  // and converted to Attributes), and trailing non-Doc comments are
  // supposed to be allowed. Therefore, the rule will eventually be
  // removed, once the valid issues it currently surfaces have been
  // addressed.
  linter.ExcludeCheckId("no-trailing-comment");

  bool added_excludes = false;
  for (auto check_id : options.excluded_checks) {
    added_excludes = true;
    linter.ExcludeCheckId(check_id);
  }

  // Includes will override excludes
  bool added_includes = false;
  for (auto check_id : options.included_checks) {
    added_includes = true;
    linter.IncludeCheckId(check_id);
  }

  if (added_includes && !added_excludes) {
    linter.set_exclude_by_default(true);
  }

  if (linter.Lint(ast, findings)) {
    return true;
  }
  return false;
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

  if (filepaths.size() == 0) {
    FailWithUsage(argv[0], "No files provided\n");
  }

  fidl::SourceManager source_manager;

  // Process filenames.
  for (auto filepath : filepaths) {
    if (!source_manager.CreateSource(filepath)) {
      Fail("Couldn't read in source data from %s\n", filepath.c_str());
    }
  }

  fidl::Findings findings;
  fidl::ErrorReporter error_reporter;
  for (const auto& source_file : source_manager.sources()) {
    std::string output;
    if (!Lint(options, *source_file, &findings, &error_reporter, output)) {
      // Some findings were produced but for now we will continue,
      // and print the results at the end.
    }
  }
  if (options.format == "text") {
    fidl::utils::WriteFindingsToErrorReporter(findings, &error_reporter);
    error_reporter.PrintReports();
  } else {
    assert(options.format == "json");  // should never be false
    std::cout << fidl::FindingsJson(findings).Produce().str();
  }
}
