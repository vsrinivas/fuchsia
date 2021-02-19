// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/cmdline.h>
#include <platform.h>
#include <stdio.h>
#include <zircon/assert.h>

// TODO(53594): A no-op destination for spurious warnings for the duration of
// the migration.
struct NullFile {
  int Write(ktl::string_view s) { return static_cast<int>(s.size()); }
};

void ParseBootOptions(ktl::string_view cmdline) {
  static BootOptions boot_options;
  // TODO(53594): Set `complain` to `stdout` once we have migrated all
  // cmdline arguments to BootOptions; until then, we suppress spurious
  // complaints about unrecognized options.
  NullFile null;
  FILE complain{&null};
  boot_options.SetMany(cmdline, &complain);
  gBootOptions = &boot_options;

  // Note: it is intentional that we build up `gBootOptions` before
  // `gCmdline`, as the former can redact information of which we do not
  // multiple instances (e.g., kernel.entropy-mixin).
  ZX_ASSERT(cmdline.back() == '\0');
  gCmdline.Append(cmdline.data());
}
