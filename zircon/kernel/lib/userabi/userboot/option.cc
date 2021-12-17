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
constexpr std::string_view kShutdownOpt = "userboot.shutdown";
constexpr std::string_view kRebootOpt = "userboot.reboot";

// TODO(joshuaseaton): This should really be defined as a default value of
// `Options::next` and expressed as a std::string_view; however, that can
// sometimes generate a writable data section. While such sections are
// prohibited, we apply the default within ParseCmdline() below and keep this
// value as a char array.
constexpr const char kNextDefault[] = "bin/bootsvc";

struct KeyAndValue {
  std::string_view key, value;
};

KeyAndValue SplitOpt(std::string_view opt) {
  std::string_view key = opt.substr(0, opt.find_first_of('='));
  opt.remove_prefix(std::min(opt.size(), key.size() + 1));
  return {key, opt};
}

}  // namespace

void ParseCmdline(const zx::debuglog& log, std::string_view cmdline, Options& opts) {
  if (opts.next.empty()) {
    opts.next = kNextDefault;
  }

  for (std::string_view opt : WordView(cmdline)) {
    if (!cpp20::starts_with(opt, kOptPrefix)) {
      continue;
    }
    auto [key, value] = SplitOpt(opt);
    if (key == kNextOpt) {
      opts.next = value;
    } else if (key == kRootOpt) {
      // Normalize away a trailing '/', if present.
      if (!value.empty() && value.back() == '/') {
        value.remove_suffix(1);
      }
      opts.root = value;
    } else if (key == kShutdownOpt) {
      opts.epilogue = Epilogue::kPowerOffAfterChildExit;
    } else if (key == kRebootOpt) {
      opts.epilogue = Epilogue::kRebootAfterChildExit;
    } else {
      printl(log, "WARNING: unknown option %.*s ignored\n", static_cast<int>(key.size()),
             key.data());
      continue;
    }
    printl(log, "OPTION %.*s%s%.*s\n", static_cast<int>(key.size()), key.data(),
           value.empty() ? "" : "=", static_cast<int>(value.size()), value.data());
  }
}
