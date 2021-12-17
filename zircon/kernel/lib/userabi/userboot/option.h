// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_OPTION_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_OPTION_H_

#include <lib/zx/debuglog.h>

#include <string_view>

// Userboot's terminal behaviour.
enum class Epilogue {
  kExitAfterChildLaunch,
  kRebootAfterChildExit,    // If `userboot.reboot` is set.
  kPowerOffAfterChildExit,  // If `userboot.shutdown` is set.
};

// Userboot options, as determined by a ZBI's CMDLINE payloads.
struct Options {
  // `userboot.root`: the BOOTFS directory under which userboot will find its
  // child program and the libraries accessible to its loader service
  std::string_view root;

  // `userboot.next`: The root-relative child program path.
  std::string_view next;

  Epilogue epilogue = Epilogue::kExitAfterChildLaunch;
};

// Parses the provided CMDLINE payload for userboot options.
void ParseCmdline(const zx::debuglog& log, std::string_view cmdline, Options& opts);

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_OPTION_H_
