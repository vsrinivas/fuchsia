// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_OPTION_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_OPTION_H_

#include <lib/zx/debuglog.h>

#include <string_view>

// Userboot options, as determined by a ZBI's CMDLINE payloads.
struct Options {
  struct ProgramInfo {
    // `prefix.root`: the BOOTFS directory under which userboot will find its
    // child program and the libraries accessible to its loader service
    std::string_view root;

    // `prefix.next`: The root-relative child program path, with optional '+' separated
    // arguments to pass to the child program.
    std::string_view next;

    constexpr std::string_view filename() const { return next.substr(0, next.find('+')); }
  };

  // Optional Program to be executed and handed control to.
  // Userboot will provide the SvcStash Handle to this elf binary.
  // prefix: `userboot`
  ProgramInfo boot;

  // Optional Program to be executed before the booting program.
  // prefix: `userboot.test`
  ProgramInfo test;
};

// Parses the provided CMDLINE payload for userboot options.
void ParseCmdline(const zx::debuglog& log, std::string_view cmdline, Options& opts);

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_OPTION_H_
