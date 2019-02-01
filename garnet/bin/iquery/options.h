// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_OPTIONS_H_
#define GARNET_BIN_IQUERY_OPTIONS_H_

#include <memory>

#include <lib/fxl/command_line.h>

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

  Options::Mode mode = Options::Mode::UNSET;

  PathFormatting path_format = Options::PathFormatting::NONE;

  bool recursive = false;

  std::vector<std::string> paths;

  FormatterType formatter_type;
  std::unique_ptr<iquery::Formatter> formatter;

  Options(const fxl::CommandLine& command_line);

  bool Valid() { return valid_; }
  void Usage(const std::string& argv0);

 private:
  bool SetMode(const fxl::CommandLine& command_line, Mode m);
  void Invalid(const std::string& argv0, std::string reason);

  bool valid_ = false;
};

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_OPTIONS_H_
