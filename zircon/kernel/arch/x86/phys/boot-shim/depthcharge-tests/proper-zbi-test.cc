// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/zbi-boot.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <string_view>

#include <ktl/algorithm.h>
#include <ktl/string_view.h>

#include "test-main.h"

// Checks that the |zbi| handed from depthcharge multiboot shim are proper.
int TestMain(void* zbi, arch::EarlyTicks ticks) {
  MainSymbolize symbolize("depthcharge-proper-zbi-test");
  zbitl::View<zbitl::ByteView> zbi_view{
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi))};

  int i = 0;
  int buggy_item_index_1 = 0;
  int buggy_item_index_2 = 0;
  for (const auto& [header, payload] : zbi_view) {
    switch (header->type) {
      case arch::kZbiBootKernelType:
        ZX_ASSERT(i == 0);
        break;
      case ZBI_TYPE_BOOTLOADER_FILE:
        ktl::string_view name(reinterpret_cast<const char*>(&payload[1]),
                              static_cast<size_t>(payload[0]));
        ktl::string_view content(reinterpret_cast<const char*>(&payload[1 + name.length()]),
                                 static_cast<size_t>(header->length - name.length() - 1));
        if (name == "foo" && content == "bar") {
          buggy_item_index_1 = i;
        } else if (name == "fooz" && content == "barz") {
          buggy_item_index_2 = i;
        }
        break;
    }
    i++;
  }
  ZX_ASSERT_MSG(buggy_item_index_1 == i - 2, "buggy item(--entry=$03foobar) at %d expected at %d",
                buggy_item_index_1, i - 2);
  ZX_ASSERT_MSG(buggy_item_index_2 == i - 1, "buggy item(--entry=$04foozbarz) at %d expected at %d",
                buggy_item_index_2, i - 1);
  ZX_ASSERT(zbi_view.take_error().is_ok());
  return 0;
}
