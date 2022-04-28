// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_status_page.h"

void GlobalHardwareStatusPage::ReadContextStatus(uint64_t& read_index,
                                                 std::optional<bool>* idle_out) {
  auto context_status_ptr =
      reinterpret_cast<volatile uint64_t*>(hardware_status_page_cpu_addr()) + 8;

  uint64_t last_written_status_index = context_status_ptr[7] >> 32;
  DASSERT((last_written_status_index & ~0x7) == 0);

  int64_t count = last_written_status_index - read_index;

  if (count < 0) {
    count += 6;
  }

  for (int64_t i = 0; i < count; i++) {
    uint32_t index = (read_index + 1 + i) % 6;

    uint64_t status = context_status_ptr[index];

    if (status & 1)
      *idle_out = false;

    else if (status & (1 << 3))
      *idle_out = true;
  }

  read_index = last_written_status_index;
}
