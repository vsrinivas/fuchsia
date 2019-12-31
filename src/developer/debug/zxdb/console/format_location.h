// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_LOCATION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_LOCATION_H_

#include <string>

#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class FileLine;
class Location;
class Target;
class TargetSymbols;

struct FormatLocationOptions {
  // Use the default values.
  FormatLocationOptions() = default;

  // Take the default values from the settings that apply to location formatting. The Target can
  // be null for the default behavior (this simplifies some call sites).
  explicit FormatLocationOptions(const Target* target);

  // How identifier function name formatting should be done.
  FormatFunctionNameOptions func;

  // When set, the address will always be printed. Otherwise it will be omitted if there is a
  // function name present.
  bool always_show_addresses = false;

  // Show function parameter types. Otherwise, it will have "()" (if there are no arguments), or
  // "(...)" if there are some.
  bool show_params = false;

  // Shows file/line information if present.
  bool show_file_line = true;

  // When set forces the file/line (if displayed) to show the full path of the file rather than
  // the shortest possible unique one.
  bool show_file_path = false;

  // Needed when show_file_path is NOT set to shorten paths. This will be used to disambiguate file
  // names. If unset, it will be equivalent to show_file_path = true.
  const TargetSymbols* target_symbols = nullptr;
};

// Formats the location.
OutputBuffer FormatLocation(const Location& loc, const FormatLocationOptions& opts);

// The TargetSymbols pointer is used to find the shortest unique way to reference the file name.
//
// If target_symbols is null, the full file path will always be included.
std::string DescribeFileLine(const TargetSymbols* optional_target_symbols,
                             const FileLine& file_line);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_LOCATION_H_
