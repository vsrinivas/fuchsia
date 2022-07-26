// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "option.h"

#include <lib/boot-options/word-view.h>
#include <lib/stdcompat/string_view.h>

#include "util.h"

namespace {

constexpr std::string_view kOptPrefix = "userboot";
constexpr std::string_view kRootOpt = "userboot.root";
constexpr std::string_view kNextOpt = "userboot.next";
constexpr std::string_view kTestRootOpt = "userboot.test.root";
constexpr std::string_view kTestNextOpt = "userboot.test.next";

// TODO(joshuaseaton): This should really be defined as a default value of
// `Options::next` and expressed as a std::string_view; however, that can
// sometimes generate a writable data section. While such sections are
// prohibited, we apply the default within ParseCmdline() below and keep this
// value as a char array.
constexpr const char kNextDefault[] = "bin/component_manager+--boot";

struct KeyAndValue {
  std::string_view key, value;
};

KeyAndValue SplitOpt(std::string_view opt) {
  std::string_view key = opt.substr(0, opt.find('='));
  opt.remove_prefix(std::min(opt.size(), key.size() + 1));
  return {key, opt};
}

constexpr std::string_view NormalizePath(std::string_view view) {
  if (view.empty() || view.back() != '/') {
    return view;
  }
  return view.substr(0, view.length() - 1);
}

constexpr bool ParseOption(std::string_view key, std::string_view value, Options& opts) {
  if (key == kNextOpt) {
    opts.boot.next = value;
    return true;
  }

  if (key == kRootOpt) {
    opts.boot.root = NormalizePath(value);
    return true;
  }

  if (key == kTestNextOpt) {
    opts.test.next = value;
    return true;
  }

  if (key == kTestRootOpt) {
    opts.test.root = NormalizePath(value);
    return true;
  }

  return false;
}

}  // namespace

void ParseCmdline(const zx::debuglog& log, std::string_view cmdline, Options& opts) {
  for (std::string_view opt : WordView(cmdline)) {
    if (!cpp20::starts_with(opt, kOptPrefix)) {
      continue;
    }

    auto [key, value] = SplitOpt(opt);
    if (ParseOption(key, value, opts)) {
      printl(log, "OPTION %.*s%s%.*s\n", static_cast<int>(key.size()), key.data(),
             value.empty() ? "" : "=", static_cast<int>(value.size()), value.data());
    } else {
      printl(log, "WARNING: unknown option %.*s ignored\n", static_cast<int>(key.size()),
             key.data());
    }
  }

  // Only set default boot program for non test environments.
  if (opts.boot.next.empty() && opts.test.next.empty()) {
    opts.boot.next = kNextDefault;
  }

  if (!opts.boot.root.empty() && opts.boot.root.front() == '/') {
    fail(log, "`userboot.root` (\"%.*s\" must not begin with a \'/\'",
         static_cast<int>(opts.boot.root.size()), opts.boot.root.data());
  }
}
