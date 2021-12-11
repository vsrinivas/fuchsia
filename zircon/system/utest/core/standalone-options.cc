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
