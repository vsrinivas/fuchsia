// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/ledger_client/page_id.h"

namespace modular {

ledger::PageId MakePageId(const std::string& value) {
  ledger::PageId page_id;
  memset(page_id.id.mutable_data(), 0, page_id.id.count());
  size_t size = std::min(value.length(), page_id.id.count());
  memcpy(page_id.id.mutable_data(), value.data(), size);
  return page_id;
}

}  // namespace modular
