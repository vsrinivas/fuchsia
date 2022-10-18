// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/debugdata.h>
#include <lib/zbitl/items/debugdata.h>
#include <stdio.h>
#include <turducken.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/array.h>
#include <ktl/string_view.h>

#include "debugdata-info.h"

// Declared in turducken.h.
const char* kTestName = "debug-data-provider";

// This commandline is added, once the debug data provider has
// added the debug data item, and booted into itself, such that the boot_zbi
// has the debug data items already.
constexpr ktl::string_view kDebugdataProvided = "debug-data-provider.done=";

int TurduckenTest::Main(Zbi::iterator kernel_item) {
  constexpr ktl::string_view kTrue = "true";
  auto maybe_done = OptionWithPrefix(kDebugdataProvided);
  if (*maybe_done == kTrue) {
    return 0;
  }

  uint32_t extra_capacity = 0;
  for (const auto& item : kDebugdataItems) {
    extra_capacity +=
        static_cast<uint32_t>(sizeof(zbi_header_t) + item.aligned_size() + sizeof(zbi_debugdata_t));
  }

  // Command line item marking debug data as set. Account for null terminator that will be added by
  // snprintf.
  extra_capacity += static_cast<uint32_t>(
      sizeof(zbi_header_t) + ZBI_ALIGN(kDebugdataProvided.length() + kTrue.length() + 1));
  Load(kernel_item, kernel_item, kernel_item.view().end(), extra_capacity);

  // Add the cmdline
  auto it_or = loaded_zbi().Append(
      {.type = ZBI_TYPE_CMDLINE,
       .length = static_cast<uint32_t>(kDebugdataProvided.length() + kTrue.length() + 1)});
  ZX_ASSERT(it_or.is_ok());

  // It is safe to use as c_str, since the used string_view references a c_str that is null
  // terminated.
  ZX_ASSERT(snprintf(reinterpret_cast<char*>(it_or->payload.data()), it_or->payload.size_bytes(),
                     "%s%s", kDebugdataProvided.data(),
                     kTrue.data()) == kDebugdataProvided.length() + kTrue.length());

  // Add the debug data items.
  auto zbi = loaded_zbi();
  for (const auto& debugdata_item : kDebugdataItems) {
    boot_shim::DebugdataItem item;
    item.Init(debugdata_item.sink, debugdata_item.vmo_name);
    item.set_log(debugdata_item.log);
    item.set_content_size(debugdata_item.payload.size());
    ZX_ASSERT(item.AppendItems(zbi).is_ok());
    auto contents = item.contents();
    memcpy(contents.data(), debugdata_item.payload.data(), contents.size());
  }

  Boot();
  __UNREACHABLE;
}
