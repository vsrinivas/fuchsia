// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "stdout.h"

#include <lib/boot-options/boot-options.h>
#include <lib/boot-options/word-view.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/uart/all.h>

namespace {

lazy_init::LazyInit<uart::all::KernelDriver<uart::BasicIoProvider, uart::Unsynchronized>,
                    lazy_init::CheckType::None, lazy_init::Destructor::Disabled>
    gSerial;

void UpdateStdout() {
  gSerial.Get().Visit([](auto&& driver) {
    driver.Init();
    FILE::stdout_ = FILE{&driver};
  });
}

}  // namespace

FILE FILE::stdout_;

void StdoutInit() {
  // Start with the null driver and then update it when new settings come in.
  gSerial.Initialize();
  UpdateStdout();
}

// Pure Multiboot loaders like QEMU provide no means of information about the
// serial port, just the command line.  So parse it just for kernel.serial.
void StdoutFromCmdline(ktl::string_view cmdline) {
  BootOptions boot_opts;
  boot_opts.SetMany(cmdline);
  gSerial.Get() = boot_opts.serial;
  UpdateStdout();

  // We only use boot-options parsing for kernel.serial and ignore the rest.
  // But it destructively scrubs the RedactedHex input so we have to undo that.
  if (boot_opts.entropy_mixin.len > 0) {
    // BootOptions already parsed and redacted, so put it back.
    for (auto word : WordView(cmdline)) {
      constexpr ktl::string_view kPrefix = "kernel.entropy-mixin=";
      if (ktl::starts_with(word, kPrefix)) {
        word.remove_prefix(kPrefix.length());
        memcpy(const_cast<char*>(word.data()), boot_opts.entropy_mixin.hex.data(),
               std::min(boot_opts.entropy_mixin.len, word.size()));
        boot_opts.entropy_mixin = {};
        break;
      }
    }
  }
}
