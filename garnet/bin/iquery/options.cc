// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/iquery/options.h"

#include <lib/inspect/query/json_formatter.h>
#include <lib/inspect/query/text_formatter.h>
#include <src/lib/files/path.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/fxl/strings/substitute.h>

#include <iostream>
#include <set>
#include <string>

#include "lib/inspect/query/formatter.h"

namespace iquery {

namespace {

std::set<std::string> kKnownOptions = {
    "cat",  "absolute_paths", "find",    "format", "full_paths", "help",
    "ls",   "recursive",      "verbose", "quiet",  "log-file",   "dir",
    "sort", "report",         "health",
};

// Validate whether the option is within the defined ones.
bool OptionExists(const std::string& option) {
  if (kKnownOptions.find(option) == kKnownOptions.end()) {
    FXL_LOG(ERROR) << "Unknown option \"" << option << "\"";
    return false;
  }
  return true;
}

Options::FormatterType GetFormatterType(const fxl::CommandLine& cmd_line) {
  std::string formatter = cmd_line.GetOptionValueWithDefault("format", "");
  if (formatter.empty() || formatter == "text") {
    return Options::FormatterType::TEXT;
  } else if (formatter == "json") {
    return Options::FormatterType::JSON;
  } else {
    FXL_LOG(ERROR) << "Cannot find formatter: " << formatter;
    return Options::FormatterType::UNSET;
  }
}

std::unique_ptr<inspect::Formatter> CreateFormatter(
    Options::FormatterType type,
    const inspect::Formatter::PathFormat& path_format) {
  switch (type) {
    case Options::FormatterType::TEXT: {
      inspect::TextFormatter::Options options;
      return std::make_unique<inspect::TextFormatter>(options, path_format);
    }
    case Options::FormatterType::JSON: {
      inspect::JsonFormatter::Options options;
      return std::make_unique<inspect::JsonFormatter>(options, path_format);
    }
    case Options::FormatterType::UNSET:
      return nullptr;
  }
  return nullptr;
}

}  // namespace

Options::Options(const fxl::CommandLine& command_line) {
  // Validate options
  for (const fxl::CommandLine::Option& option : command_line.options()) {
    if (!OptionExists(option.name))
      return;
  }

  command_line.GetOptionValue("dir", &chdir);

  bool is_recursive_set = command_line.HasOption("recursive");

  if (command_line.HasOption("health")) {
    health = true;
    depth_ = is_recursive_set ? -1 : 1;
    mode = iquery::Options::Mode::HEALTH;
  } else if (command_line.HasOption("report")) {
    report = true;
    path_format = inspect::Formatter::PathFormat::ABSOLUTE;
    depth_ = -1;
    sort = true;
    mode = iquery::Options::Mode::CAT;
    paths = {};
  } else {
    if (command_line.HasOption("cat") && !SetMode(command_line, Mode::CAT))
      return;
    else if (command_line.HasOption("find") &&
             !SetMode(command_line, Mode::FIND))
      return;
    else if (command_line.HasOption("ls") && !SetMode(command_line, Mode::LS))
      return;
    else if (mode == Mode::UNSET)
      SetMode(command_line, Mode::CAT);

    // Path formatting options.
    path_format = inspect::Formatter::PathFormat::NONE;
    if (command_line.HasOption("full_paths")) {
      path_format = inspect::Formatter::PathFormat::FULL;
    }
    if (command_line.HasOption("absolute_paths")) {
      path_format = inspect::Formatter::PathFormat::ABSOLUTE;
    }

    // Find has a special case, where none path formatting is not really useful.
    if (path_format == inspect::Formatter::PathFormat::NONE &&
        mode == Mode::FIND) {
      path_format = inspect::Formatter::PathFormat::FULL;
    }

    depth_ = is_recursive_set ? -1 : 0;
    sort = command_line.HasOption("sort");
  }

  formatter_type = GetFormatterType(command_line);
  formatter = CreateFormatter(formatter_type, path_format);
  if (!formatter)
    return;

  std::copy(command_line.positional_args().begin(),
            command_line.positional_args().end(), std::back_inserter(paths));

  // If everything went well, we mark this options as valid.
  valid_ = true;
}

void Options::Usage(const std::string& argv0) {
  std::cout << fxl::Substitute(
      R"txt(Usage: $0 (--cat|--find|--ls) [--recursive] [--sort]
      [--format=<FORMAT>] [(--full_paths|--absolute_paths)] [--dir=<PATH>]
      PATH [...PATH]

  Utility for querying exposed object directories.

  Global options:
  --dir:     Change directory to the given PATH before executing commands.

  Mode options:
  --cat:    [DEFAULT] Print the data for the object(s) given by each PATH.
            Specifying --recursive will also output the children for that object.
  --find:   find all objects under PATH. For each sub-path, will stop at finding
            the first object. Specifying --recursive will search the whole tree.
  --health: Output a report that scans the system looking for health nodes and
            showing the status of them.
  --ls:     List the children of the object(s) given by PATH. Specifying
            --recursive has no effect.
  --report: Output a default report for all components on the system. Ignores all
            settings other than --format.

  --recursive: Whether iquery should continue inside an object. See each mode's
               description to see how it modifies their behaviors.

  Formatting:
  --format: What formatter to use for output. Available options are:
    - text: [DEFAULT] Simple text output meant for manual inspection.
    - json: JSON format meant for machine consumption.

  --sort: Whether iquery should sort children by name before printing.

  --full_paths:     Include the full path in object names.
  --absolute_paths: Include full absolute path in object names.
                    Overrides --full_paths.

  PATH: paths where to look for targets. The interpretation of those depends
        on the mode.
)txt",
      argv0);
}

bool Options::SetMode(const fxl::CommandLine& command_line, Mode m) {
  if (mode != Mode::UNSET) {
    Invalid(command_line.argv0(), "multiple modes specified");
    return false;
  }
  mode = m;
  return true;
}

void Options::Invalid(const std::string& argv0, std::string reason) {
  std::cerr << fxl::Substitute("Invalid command line args: $0\n", reason);
  Usage(argv0);
  valid_ = false;
}

}  // namespace iquery
