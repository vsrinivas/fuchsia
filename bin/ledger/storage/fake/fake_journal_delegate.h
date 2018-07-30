// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_

#include <map>
#include <string>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/convert/convert.h"

namespace storage {
namespace fake {

// |FakeJournalDelegate| records the changes made through a journal. This
// object is owned by |FakePageStorage| and outlives |FakeJournal|.
class FakeJournalDelegate {
 public:
  using Data = std::map<std::string, Entry, convert::StringViewComparator>;

  // Regular commit.
  // |initial_data| must contain the content of the page when the transaction
  // starts.
  FakeJournalDelegate(Data initial_data, CommitId parent_id, bool autocommit,
                      uint64_t generation);
  // Merge commit.
  // |initial_data| must contain the content of the page when the transaction
  // starts.
  FakeJournalDelegate(Data initial_data, CommitId parent_id, CommitId other_id,
                      bool autocommit, uint64_t generation);
  ~FakeJournalDelegate();

  const CommitId& GetId() const { return id_; }

  Status SetValue(convert::ExtendedStringView key, ObjectIdentifier value,
                  KeyPriority priority);
  Status Delete(convert::ExtendedStringView key);
  Status Clear();

  void Commit(
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);
  bool IsCommitted() const;

  Status Rollback();
  bool IsRolledBack() const;

  uint64_t GetGeneration() const { return generation_; }

  std::vector<CommitIdView> GetParentIds() const;

  bool IsPendingCommit();
  void ResolvePendingCommit(Status status);

  const Data& GetData() const;

 private:
  bool autocommit_;

  const CommitId id_;
  const CommitId parent_id_;
  const CommitId other_id_;
  Data data_;
  uint64_t generation_;

  bool is_committed_ = false;
  bool is_rolled_back_ = false;
  fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
      commit_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeJournalDelegate);
};

}  // namespace fake
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_
