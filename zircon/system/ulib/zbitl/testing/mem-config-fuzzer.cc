// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/memory.h>
#include <lib/zbitl/view.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  zbitl::View view(zbitl::AsBytes(data, size));

  // Iterate through the table.
  zbitl::MemRangeTable table{view};
  for (const auto& item : table) {
    (void)item;
  }

  // Ignore any errors.
  (void)table.take_error();

  return 0;
}
