// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_INSPECT_INSPECT_H_
#define SRC_LEDGER_BIN_INSPECT_INSPECT_H_

#include <string>

#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

// The filesystem directory in which the Inspect hierarchy appears.
inline absl::string_view kInspectNodesDirectory = "diagnostics";
// The name given to the top-level object at the root of the Inspect hierarchy.
inline absl::string_view kTopLevelNodeName = "ledger_component";
inline absl::string_view kRepositoriesInspectPathComponent = "repositories";
inline absl::string_view kLedgersInspectPathComponent = "ledgers";
inline absl::string_view kPagesInspectPathComponent = "pages";
inline absl::string_view kHeadsInspectPathComponent = "heads";
inline absl::string_view kCommitsInspectPathComponent = "commits";
inline absl::string_view kParentsInspectPathComponent = "parents";
inline absl::string_view kEntriesInspectPathComponent = "entries";
inline absl::string_view kValueInspectPathComponent = "value";
// TODO(nathaniel): "requests" was introduced as a demonstration; it should be
// either given real meaning or cleaned up.
inline absl::string_view kRequestsInspectPathComponent = "requests";

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
