// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_

#include <lib/fit/function.h>

#include <map>
#include <string>

#include "peridot/lib/rng/random.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/macros.h"

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
  FakeJournalDelegate(rng::Random* random, FakeObjectIdentifierFactory* factory, Data initial_data,
                      CommitId parent_id, bool autocommit, uint64_t generation);
  // Merge commit.
  // |initial_data| must contain the content of the page when the transaction
  // starts.
  FakeJournalDelegate(rng::Random* random, FakeObjectIdentifierFactory* factory, Data initial_data,
                      CommitId parent_id, CommitId other_id, bool autocommit, uint64_t generation);
  ~FakeJournalDelegate();

  const CommitId& GetId() const { return id_; }

  void SetValue(convert::ExtendedStringView key, ObjectIdentifier value, KeyPriority priority);
  void Delete(convert::ExtendedStringView key);
  void Clear();

  void Commit(fit::function<void(Status, std::unique_ptr<const storage::Commit>)> callback);
  bool IsCommitted() const;

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
  fit::function<void(Status, std::unique_ptr<const storage::Commit>)> commit_callback_;

  FakeObjectIdentifierFactory* factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeJournalDelegate);
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_
