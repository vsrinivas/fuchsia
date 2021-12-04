// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/cmdline.h>
#include <platform.h>
#include <zircon/assert.h>

namespace {

BootOptions gBootOptionsInstance;

}  // namespace

void ParseBootOptions(ktl::string_view cmdline) {
  // Because we don't know if we have looked at the zbi entries yet, the best thing is
  // to recheck the zbi if no entry in the command line is found, whenever the serial is being
  // parsed.
  if (gBootOptionsInstance.serial_source == OptionSource::kDefault) {
    gBootOptionsInstance.serial_source = OptionSource::kZbi;
  }

  gBootOptionsInstance.SetMany(cmdline);

  // Note: it is intentional that we build up `gBootOptions` before
  // `gCmdline`, as the former can redact information of which we do not
  // multiple instances (e.g., kernel.entropy-mixin).
  ZX_ASSERT(cmdline.back() == '\0');
  gCmdline.Append(cmdline.data());
}

void FinishBootOptions() { gBootOptions = &gBootOptionsInstance; }
