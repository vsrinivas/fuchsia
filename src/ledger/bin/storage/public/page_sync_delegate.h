// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_

#include <lib/fit/function.h>

#include <functional>

#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {

// The type of the object a piece belongs to. A piece can be part of multiple objects. In this
// should be |TREE_NODE| if we are trying to read this piece because we are reading a tree node, and
// |BLOB| if we are trying to read it as part of a value.
enum class RetrievedObjectType {
  TREE_NODE,
  BLOB,
};

// Delegate interface for PageStorage responsible for retrieving on-demand
// storage objects from the network (cloud or P2P).
class PageSyncDelegate {
 public:
  PageSyncDelegate() = default;
  PageSyncDelegate(const PageSyncDelegate&) = delete;
  PageSyncDelegate& operator=(const PageSyncDelegate&) = delete;
  virtual ~PageSyncDelegate() = default;

  // Retrieves the piece of the given id from the network.
  //
  // |retrieved_object_type| is |TREE_NODE| if the piece is part of a tree node, and |BLOB|
  // otherwise. If |retrieved_object_type| is |TREE_NODE|, the piece will not be retrieved from the
  // cloud.
  //
  // Compatibility: the client may set |retrieved_object_type| to |BLOB| for parts of tree nodes to
  // force retrieving a piece from the cloud even if it is part of the tree.
  // TODO(LE-823): remove compatibility.
  virtual void GetObject(ObjectIdentifier object_identifier,
                         RetrievedObjectType retrieved_object_type,
                         fit::function<void(Status, ChangeSource, IsObjectSynced,
                                            std::unique_ptr<DataSource::DataChunk>)>
                             callback) = 0;

  // Retrieves the diff for the given commit from the network.
  //
  // |possible_bases| is a list of commits the storage expects to get a diff from. The cloud may
  // choose any of these commits or the root commit as a base.  |callback| is called with a status,
  // and if the call is successful, with the id of the commit chosen as a base, and the list of
  // changes in the diff.
  //
  // Compatibility:
  // - the cloud can use a base commit that is not in |possible_bases|. The tree of this commit can
  //   be retrieved from the cloud using |GetObject| with |BLOB| as object type.
  // - if the cloud does not support diffs, or has no diff available for this commit because the
  //   client that uploaded it did not support diffs, it must return an empty diff for the same
  //   commit. Then the previous behavior will apply.
  // TODO(LE-823): remove compatibility.
  virtual void GetDiff(
      CommitId commit_id, std::vector<CommitId> possible_bases,
      fit::function<void(Status, CommitId, std::vector<EntryChange>)> callback) = 0;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_
