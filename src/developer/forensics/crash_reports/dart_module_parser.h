// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DART_MODULE_PARSER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DART_MODULE_PARSER_H_

#include <optional>
#include <string>
#include <string_view>

namespace forensics::crash_reports {

// Parses Dart Module information from |stack_trace|, if it is a valid, unsymbolicated Dart
// stack trace.
//
// The first value of the pair indicates if |stack_trace| is an unsymbolicated Dart stack trace
// (true means yes) and the second value is the module information parsed from the stack trace, if
// parsing doesn't fail. We expect symbolized and unsymbolicated stack traces so we only want to
// warn on unexpected parsing failures.
std::pair<bool, std::optional<std::string>> ParseDartModulesFromStackTrace(
    std::string_view stack_trace);

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DART_MODULE_PARSER_H_
