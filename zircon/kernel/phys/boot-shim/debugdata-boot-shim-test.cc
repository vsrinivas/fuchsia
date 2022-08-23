// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/zbi-boot.h>
#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/debugdata.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/items/debugdata.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <ktl/iterator.h>
#include <ktl/string_view.h>
#include <phys/symbolize.h>

#include "turducken.h"

#include <ktl/enforce.h>

using Shim = boot_shim::BootShim<boot_shim::DebugdataItem>;

const char* kTestName = "debugdata-boot-shim-test";

// When given this option...
constexpr ktl::string_view kPublishOption = "debugdata.publish";

// ...emit this ZBI_TYPE_DEBUGDATA contents to the next iteration.
// Without the publish option, check that such an item was received.
constexpr ktl::string_view kSinkName = "test-sink";
constexpr ktl::string_view kVmoName = "test-debugdata";
constexpr ktl::string_view kContents = "test debugdata contents";
constexpr ktl::string_view kLog = "test log text\nsecond line of log";

int TurduckenTest::Main(Zbi::iterator kernel_item) {
  Shim shim(gSymbolize->name());
  shim.set_build_id(gSymbolize->BuildIdString());

  LogBootZbiItems(kernel_item);

  // First time through, publish the debugdata.
  if (Option(kPublishOption)) {
    RemoveOption(kPublishOption);

    auto& debugdata = shim.Get<boot_shim::DebugdataItem>();
    debugdata.Init(kSinkName, kVmoName);
    debugdata.set_content_size(kContents.size());
    debugdata.set_log(kLog);
    printf("%s: Initialized item of %zu bytes\n", gSymbolize->name(), debugdata.size_bytes());

    // Pass along the original items after the embedded ZBI, plus the new
    // debugdata item.
    printf("%s: Loading embedded ZBI with %zu bytes extra capacity...\n", gSymbolize->name(),
           shim.size_bytes());
    Load(kernel_item, ktl::next(kernel_item), boot_zbi().end(),
         static_cast<uint32_t>(shim.size_bytes()));

    printf("%s: Adding ZBI_TYPE_DEBUGDATA item...\n", gSymbolize->name());
    Shim::DataZbi zbi(loaded_zbi());
    if (auto result = shim.AppendItems(zbi); result.is_error()) {
      printf("%s: shim.AppendItems failed: ", gSymbolize->name());
      zbitl::PrintViewError(result.error_value());
      return -1;
    }
    ZX_ASSERT(zbi.take_error().is_ok());

    kContents.copy(reinterpret_cast<char*>(debugdata.contents().data()),
                   debugdata.contents().size());

    Boot();
  }

  // Second time through, check the data from last time.
  printf("%s: checking ZBI_TYPE_DEBUGDATA item...\n", gSymbolize->name());

  ktl::optional<zbitl::Debugdata> debugdata;
  for (auto [header, payload] : boot_zbi()) {
    if (header->type == ZBI_TYPE_DEBUGDATA) {
      ZX_ASSERT_MSG(!debugdata, "hit second ZBI_TYPE_DEBUGDATA item");
      debugdata.emplace();
      auto result = debugdata->Init(payload);
      ZX_ASSERT_MSG(result.is_ok(), "%.*s", static_cast<int>(result.error_value().size()),
                    result.error_value().data());
    }
  }
  if (auto result = boot_zbi().take_error(); result.is_error()) {
    printf("%s: ZBI iteration error: ", gSymbolize->name());
    zbitl::PrintViewError(result.error_value());
    return -1;
  }
  ZX_ASSERT_MSG(debugdata, "no ZBI_TYPE_DEBUGDATA item found");

  ZX_ASSERT(debugdata->sink_name() == kSinkName);
  ZX_ASSERT(debugdata->vmo_name() == kVmoName);
  ZX_ASSERT(debugdata->log() == kLog);
  const ktl::string_view contents{
      reinterpret_cast<const char*>(debugdata->contents().data()),
      debugdata->contents().size(),
  };
  ZX_ASSERT(contents == kContents);

  // If there's an embedded ZBI, boot it as is so it can start up and see that
  // same ZBI_TYPE_DEBUGDATA item that we just checked.
  if (kernel_item != boot_zbi().end()) {
    printf("%s: chain-loading next kernel...\n", gSymbolize->name());
    Load(kernel_item, ktl::next(kernel_item), boot_zbi().end());
    Boot();
  }

  // If nothing else is embedded, the test is done.
  return 0;
}
