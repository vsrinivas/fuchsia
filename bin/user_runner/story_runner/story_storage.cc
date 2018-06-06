// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/story_storage.h"

#include <fuchsia/modular/internal/cpp/fidl.h>
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/user_runner/story_runner/story_storage_xdr.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {

StoryStorage::StoryStorage(LedgerClient* ledger_client, LedgerPageId page_id)
    : PageClient("StoryStorage", ledger_client, page_id, kStoryKeyPrefix),
      ledger_client_(ledger_client) {
  FXL_DCHECK(ledger_client_ != nullptr);
}

// |PageClient|
void StoryStorage::OnPageChange(const std::string& key,
                                const std::string& value) {}

// |PageClient|
void StoryStorage::OnPageDelete(const std::string& key) {}

}  // namespace modular
