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
inline fxl::StringView kHeadsInspectPathComponent = "heads";
inline fxl::StringView kCommitsInspectPathComponent = "commits";
inline fxl::StringView kEntriesInspectPathComponent = "entries";
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
bool PageDisplayNameToPageId(const std::string& page_display_name, storage::PageId* page_id);

// Converts a |CommitId| to a string suitable to be passed to Inspect for
// Inspect to show in its user interface and its outputs.
std::string CommitIdToDisplayName(const storage::CommitId& commit_id);

// The inverse of |CommitIdToDisplayName|, converts a string in the form
// returned by |CommitIdToDisplayName| to the |CommitId| from which the display
// name was generated and returns true. Or doesn't, because it doesn't recognize
// the display name due to some corruption or mistake, and returns false.
bool CommitDisplayNameToCommitId(const std::string& commit_display_name,
                                 storage::CommitId* commit_id);

// Converts an entry's key to a string suitable to be passed to Inspect for
// Inspect to show in its user interface and its outputs.
std::string KeyToDisplayName(const std::string& key);

// The inverse of |KeyToDisplayName|, converts a string in the form returned by
// |KeyToDisplayName| to the |std::string| from which the display name was
// generated and returns true. Or doesn't, because it doesn't recognize the
// display name due to some corruption or mistake, and returns false.
bool KeyDisplayNameToKey(const std::string& key_display_name, std::string* key);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_INSPECT_INSPECT_H_
