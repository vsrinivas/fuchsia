// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/ledger_client/page_id.h"

namespace modular {

fuchsia::ledger::PageId MakePageId(const std::string& value) {
  fuchsia::ledger::PageId page_id;
  memset(page_id.id.data(), 0, page_id.id.size());
  size_t size = std::min(value.length(), page_id.id.size());
  memcpy(page_id.id.data(), value.data(), size);
  return page_id;
}

}  // namespace modular
