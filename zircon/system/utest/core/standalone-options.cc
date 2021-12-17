// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/boot-options/word-view.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <string>

#include "standalone.h"

namespace {

BootOptions InitBootOptions() {
  zx::unowned_vmo boot_options_vmo = StandaloneGetVmo("boot-options.txt");
  ZX_ASSERT(boot_options_vmo->is_valid());
  uint64_t boot_options_size;
  zx_status_t status = boot_options_vmo->get_prop_content_size(&boot_options_size);
  ZX_ASSERT(status == ZX_OK);
  std::string boot_options_str(boot_options_size, '\0');
  status = boot_options_vmo->read(boot_options_str.data(), 0, boot_options_str.size());
  ZX_ASSERT(status == ZX_OK);
  BootOptions boot_options;
  boot_options.SetMany(boot_options_str);
  return boot_options;
}

}  // namespace

void StandaloneGetOptions(std::initializer_list<std::reference_wrapper<StandaloneOption>> opts) {
  zbitl::View zbi(StandaloneGetVmo(std::string("zbi")));
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_CMDLINE) {
      std::string str(header->length, '\0');
      zx_status_t status = zbi.storage()->read(str.data(), payload, str.size());
      ZX_ASSERT(status == ZX_OK);

      for (std::string_view word : WordView(str)) {
        for (StandaloneOption& opt : opts) {
          if (cpp20::starts_with(word, opt.prefix)) {
            opt.option = word;
          }
        }
      }
    }
  }
  zbi.ignore_error();
}

const BootOptions& StandaloneGetBootOptions() {
  // Collect the options on the first call and just return the reference later.
  static const BootOptions boot_options = InitBootOptions();
  return boot_options;
}
