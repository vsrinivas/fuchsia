// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_INSPECT_INSPECT_H_
#define SRC_LEDGER_BIN_INSPECT_INSPECT_H_

#include <string>

#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

// The filesystem directory in which the Inspect hierarchy appears.
inline fxl::StringView kInspectNodesDirectory = "inspect";
// The name given to the top-level object at the root of the Inspect hierarchy.
inline fxl::StringView kTopLevelNodeName = "ledger_component";
inline fxl::StringView kRepositoriesInspectPathComponent = "repositories";
inline fxl::StringView kLedgersInspectPathComponent = "ledgers";
inline fxl::StringView kPagesInspectPathComponent = "pages";
inline fxl::StringView kCommitsInspectPathComponent = "commits";
// TODO(nathaniel): "requests" was introduced as a demonstration; it should be
// either given real meaning or cleaned up.
inline fxl::StringView kRequestsInspectPathComponent = "requests";

// Converts a |PageId| to a string suitable to be passed to Inspect for Inspect
// to show in its user interface and its outputs.
std::string PageIdToDisplayName(const storage::PageId& page_id);

// The inverse of |PageIdToDisplayName|, converts a string in the form returned
// by |PageIdToDisplayName| to the |PageId| from which the display name was
// generated and returns true. Or doesn't, because it doesn't recognize the
// display name due to some corruption or mistake, and returns false.
bool PageDisplayNameToPageId(const std::string& page_display_name,
                             storage::PageId* page_id);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_INSPECT_INSPECT_H_
