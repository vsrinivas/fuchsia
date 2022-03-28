// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/boot-options/boot-options.h>
#include <lib/console.h>

#include <ktl/span.h>
#include <ktl/string_view.h>

#include <ktl/enforce.h>

namespace {

// Note that using this can introduce data races on the member variables.
int Set(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("Usage: %s <key>[=<value>]...\n", argv[0].str);
    return -1;
  }

  // gBootOptions is a pointer-to-const so we must const_cast in order to set options.  This is
  // inherently dangerous and racy, and should only be done in a development context.
  auto* boot_options = const_cast<BootOptions*>(gBootOptions);

  for (const auto& arg : ktl::span(argv, argc).subspan(1)) {
    boot_options->SetMany(arg.str, stdout);
  }

  return 0;
}

int Show(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc > 1) {
    int result = 0;
    for (const auto& arg : ktl::span(argv, argc).subspan(1)) {
      result |= gBootOptions->Show(ktl::string_view{arg.str});
    }
    return result;
  }

  gBootOptions->Show();
  return 0;
}

}  // namespace

STATIC_COMMAND_START
STATIC_COMMAND("setopt", "Set boot options (as from kernel cmdline)", Set)
STATIC_COMMAND("showopt", "Show boot options (from kernel cmdline)", Show)
STATIC_COMMAND_END(options)
