// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_OPTIONS_H_
#define GARNET_BIN_IQUERY_OPTIONS_H_

#include <memory>

#include <src/lib/fxl/command_line.h>

namespace iquery {

class Formatter;

class Options {
 public:
  enum class Mode {
    UNSET,
    CAT,
    FIND,
    LS,
  };
  enum class FormatterType {
    UNSET,
    JSON,
    TEXT,
  };
  enum class PathFormatting {
    NONE,      // Object name (filename).
    FULL,      // Relative path from the CWD.
    ABSOLUTE,  // Absolute path starting from root "/".
  };

  // Directory to change to before executing commands.
  std::string chdir;

  // The mode of operation.
  Options::Mode mode = Options::Mode::UNSET;

  // Path formatting mode.
  PathFormatting path_format = Options::PathFormatting::NONE;

  // If true, execute mode recursively.
  bool recursive = false;

  // If true, sort all children, metrics, and properties within each object.
  bool sort = false;

  // List of paths specified on the command line.
  std::vector<std::string> paths;

  // The type of formatter to use.
  FormatterType formatter_type;

  // Instance of the formatter.
  std::unique_ptr<iquery::Formatter> formatter;

  // Create |Options| by parsing the given command line.
  Options(const fxl::CommandLine& command_line);

  // Returns true if the command line was parsed correctly.
  bool Valid() { return valid_; }

  // Print out usage string to stdout.
  void Usage(const std::string& argv0);

 private:
  bool SetMode(const fxl::CommandLine& command_line, Mode m);
  void Invalid(const std::string& argv0, std::string reason);

  bool valid_ = false;
};

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_OPTIONS_H_
