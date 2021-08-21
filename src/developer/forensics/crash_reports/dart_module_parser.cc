// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/dart_module_parser.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string_view>

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics::crash_reports {
namespace {

// See
// https://github.com/dart-lang/sdk/blob/b44250c3f6fa607a52325dfdf753268fffe1dea6/runtime/vm/object.cc#L25661
// for the reference on how unsymbolicated Dart stack traces are constructed.

// The crash reporter doesn't have access at runtime to the module name of the Dart snapshot so it
// assumes the fallback we use on Fuchsia for non-shared libraries.
constexpr std::string_view kDartModulesName = "<_>";

// Unsymbolicated stack traces have 16 groups of "***" on the second line.
static const std::regex* const kUnsymbolicatedDartStackTraceRegex =
    new std::regex(R"((?:\*{3}\s{0,1}){16})");

bool MatchesUnsymbolicatedDartStackTrace(const std::vector<std::string>& lines) {
  return lines.size() >= 2 && std::regex_match(lines[1], *kUnsymbolicatedDartStackTraceRegex);
}

// Regexes and functions for extracting information from unsymbolicated Dart stack traces.
//
// Stack frame.
static const std::regex* const kStackFrameRegex =
    new std::regex(R"(\s*#\d{2} abs ([\da-f]+)(?: virt [\da-f]+)? .*$)");

std::optional<uint64_t> TryMatchStackAddress(const std::string& line) {
  std::smatch stack_frame_match;
  if (!std::regex_match(line, stack_frame_match, *kStackFrameRegex)) {
    return std::nullopt;
  }

  if (stack_frame_match.size() < 2) {
    return std::nullopt;
  }

  // The first match is the whole line, the second is the absolute address, and the third is the
  // virtual address.
  return std::strtoull(stack_frame_match[1].str().c_str(), nullptr, 16);
}

// Build id.
static const std::regex* const kBuildIdRegex = new std::regex(R"(\s*build_id: '([a-f\d]+)')");

std::optional<std::string> TryMatchBuildId(const std::string& line) {
  std::smatch build_id_match;
  if (!std::regex_match(line, build_id_match, *kBuildIdRegex)) {
    return std::nullopt;
  }

  if (build_id_match.size() < 2) {
    return std::nullopt;
  }

  // The first match is the whole line and the second is the build id
  return build_id_match[1].str();
}

// Isolate DSO base address.
static const std::regex* const kIsolateDsoBaseRegex =
    new std::regex(R"(\s*isolate_dso_base: ([\da-f]+), vm_dso_base: [\da-f]+)");

std::optional<uint64_t> TryMatchIsolateDsoBase(const std::string& line) {
  std::smatch isolate_dso_base_match;
  if (!std::regex_match(line, isolate_dso_base_match, *kIsolateDsoBaseRegex)) {
    return std::nullopt;
  }

  if (isolate_dso_base_match.size() < 2) {
    return std::nullopt;
  }

  // The first match is the whole line, the second is the isolate base address, and the third is the
  // vm base address.
  return std::strtoull(isolate_dso_base_match[1].str().c_str(), nullptr, 16);
}

std::string ToHexString(const uint64_t value) {
  std::stringstream stream;
  stream << std::hex << value;
  return stream.str();
}

// Converts build id endianness to match Breakpad's FileID::ConvertIDentifierToUUIDString() because
// symbol lookup is dependent on this identifier.
//
// \build_id| is a 32-bytes hex UUID without hyphens, which is 32 bytes long. It is formatted in
// groups of 8-4-4-4-12 characters. The first three groups must be big endian.
//
// Also appends a '0' to match what breakpad generates.
// https://osscs.corp.google.com/chromium/chromium/src/+/main:third_party/crashpad/crashpad/snapshot/elf/module_snapshot_elf.cc;l=153;drc=81cc8267d3a069163708f3ac140d0d940487c137
std::optional<std::string> FormatBuildId(const std::string& build_id) {
  // Build id is always 32 bytes long.
  if (build_id.size() < 16) {
    return std::nullopt;
  }

  std::string formatted{
      build_id[6],  build_id[7],  build_id[4],  build_id[5],  build_id[2], build_id[3],
      build_id[0],  build_id[1],  build_id[10], build_id[11], build_id[8], build_id[9],
      build_id[14], build_id[15], build_id[12], build_id[13],
  };

  formatted += build_id.substr(16);
  formatted += "0";

  std::transform(formatted.begin(), formatted.end(), formatted.begin(),
                 [](const unsigned char c) { return std::toupper(c); });

  return formatted;
}

}  // namespace

std::pair<bool, std::optional<std::string>> ParseDartModulesFromStackTrace(
    const std::string_view stack_trace) {
  const std::vector<std::string> lines =
      fxl::SplitStringCopy(stack_trace, "\n", fxl::WhiteSpaceHandling::kTrimWhitespace,
                           fxl::SplitResult::kSplitWantNonEmpty);

  if (!MatchesUnsymbolicatedDartStackTrace(lines)) {
    return {false, std::nullopt};
  }

  std ::optional<std::string> build_id;
  std::optional<uint64_t> isolate_dso_base;
  std::optional<uint64_t> max_address;
  for (const auto& line : lines) {
    if (const auto build_id_match = TryMatchBuildId(line); build_id_match.has_value()) {
      build_id = build_id_match;
    }

    if (const auto isolate_dso_base_match = TryMatchIsolateDsoBase(line);
        isolate_dso_base_match.has_value()) {
      isolate_dso_base = isolate_dso_base_match;
    }

    if (const auto stack_address_match = TryMatchStackAddress(line);
        stack_address_match.has_value()) {
      if (!max_address.has_value() || *stack_address_match > *max_address) {
        max_address = stack_address_match;
      }
    }
  }

  if (!build_id.has_value() || !isolate_dso_base.has_value() || !max_address.has_value()) {
    return {true, std::nullopt};
  }

  const auto identifier = FormatBuildId(build_id.value());
  if (!identifier.has_value()) {
    return {true, std::nullopt};
  }

  const std::string start_address = ToHexString(isolate_dso_base.value());
  const std::string name = std::string(kDartModulesName);

  // Estimate the length to be enough to cover every address in the stack trace.
  const std::string length = ToHexString(max_address.value() - isolate_dso_base.value() + 1);

  // Dart module information is formatted like "<startAddress>,<length>,<name>,<identifier>"
  return {true, fxl::StringPrintf("%s,%s,%s,%s", start_address.c_str(), length.c_str(),
                                  name.c_str(), identifier->c_str())};
}

}  // namespace forensics::crash_reports
