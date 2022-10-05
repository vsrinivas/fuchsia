// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/run_test_component/run_test_component.h"

#include <glob.h>
#include <lib/fit/defer.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <limits.h>

#include <sstream>
#include <string>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace run {

static constexpr char kLabelArgPrefix[] = "--realm-label=";
static constexpr char kTimeoutArgPrefix[] = "--timeout=";
static constexpr char kSeverityArgPrefix[] = "--min-severity-logs=";
static constexpr char kMaxSeverityArgPrefix[] = "--max-log-severity=";

bool to_bool(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);
  std::istringstream is(str);
  bool b;
  is >> std::boolalpha >> b;
  return b;
}

fit::result<bool, uint32_t> ParseLogLevel(const std::string& level) {
  if (level == "TRACE") {
    return fit::success(syslog::LOG_TRACE);
  }
  if (level == "DEBUG") {
    return fit::success(syslog::LOG_DEBUG);
  }
  if (level == "INFO") {
    return fit::success(syslog::LOG_INFO);
  }
  if (level == "WARN") {
    return fit::success(syslog::LOG_WARNING);
  }
  if (level == "ERROR") {
    return fit::success(syslog::LOG_ERROR);
  }
  if (level == "FATAL") {
    return fit::success(syslog::LOG_FATAL);
  }
  return fit::error(false);
}

ParseArgsResult ParseArgs(const std::shared_ptr<sys::ServiceDirectory>& services, int argc,
                          const char** argv) {
  ParseArgsResult result;
  result.error = false;
  result.timeout = -1;
  int url_or_matcher_argi = 1;

  std::string url;
  while (true) {
    if (argc < url_or_matcher_argi + 1) {
      result.error = true;
      result.error_msg = "Missing test URL, or matcher argument";
      return result;
    }

    std::string argument = argv[url_or_matcher_argi];
    const size_t kLabelArgPrefixLength = strlen(kLabelArgPrefix);
    const size_t kTimeoutArgPrefixLength = strlen(kTimeoutArgPrefix);
    const size_t kSeverityArgPrefixLength = strlen(kSeverityArgPrefix);
    const size_t kMaxSeverityArgPrefixLength = strlen(kMaxSeverityArgPrefix);

    if (argument.substr(0, kLabelArgPrefixLength) == kLabelArgPrefix) {
      result.realm_label = argument.substr(kLabelArgPrefixLength);
      url_or_matcher_argi++;
      continue;
    }

    if (argument.substr(0, kSeverityArgPrefixLength) == kSeverityArgPrefix) {
      std::string level = argument.substr(kSeverityArgPrefixLength);
      auto level_result = ParseLogLevel(level);
      if (level_result.is_error()) {
        result.error = true;
        result.error_msg = fxl::StringPrintf("Invalid --min-severity-logs %s", level.c_str());
        return result;
      }
      result.min_log_severity = level_result.value();

      url_or_matcher_argi++;
      continue;
    }

    if (argument.substr(0, kMaxSeverityArgPrefixLength) == kMaxSeverityArgPrefix) {
      std::string level = argument.substr(kMaxSeverityArgPrefixLength);
      auto level_result = ParseLogLevel(level);
      if (level_result.is_error()) {
        result.error = true;
        result.error_msg = fxl::StringPrintf("Invalid --max-log-severity %s", level.c_str());
        return result;
      }
      result.max_log_severity = level_result.value();
      url_or_matcher_argi++;
      continue;
    }

    if (argument.substr(0, kTimeoutArgPrefixLength) == kTimeoutArgPrefix) {
      std::string arg = argument.substr(kTimeoutArgPrefixLength);
      std::istringstream stream(arg);
      stream >> result.timeout;
      if (stream.fail() || result.timeout <= 0) {
        result.error = true;
        result.error_msg = fxl::StringPrintf("\"%s\" is not a valid timeout.", arg.c_str());
        return result;
      }
      url_or_matcher_argi++;
      continue;
    }

    url = argument;
    break;
  }

  result.launch_info.url = url;
  result.launch_info.arguments.emplace();
  int i = url_or_matcher_argi + 1;
  if (i < argc && std::string(argv[i]) != "--") {
    printf(
        "WARNING: Please use Option delimiter(--) before specifying test args. Current commandline "
        "will error out in future. Use\n 'run-test-component [run-test-component-args] <test_url> "
        "-- [test_args]'\n");
  } else {
    i++;
  }
  for (; i < argc; i++) {
    result.launch_info.arguments->push_back(argv[i]);
  }
  return result;
}

std::string GetSimplifiedUrl(const std::string& url) {
  component::FuchsiaPkgUrl furl;
  furl.Parse(url);
  return fxl::Substitute("fuchsia-pkg://$0/$1#$2", furl.host_name(), furl.package_name(),
                         furl.resource_path());
}

}  // namespace run
