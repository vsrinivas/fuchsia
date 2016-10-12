// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_
#define APPS_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_

#include <map>
#include <string>

#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {
namespace fake {

// |FakeJournalDelegate| records the changes made through a journal. This
// object is owned by |FakePageStorage| and outlives |FakeJournal|.
class FakeJournalDelegate {
 public:
  struct Entry {
    ObjectId value;
    bool deleted;
    KeyPriority priority;
  };

  FakeJournalDelegate();
  ~FakeJournalDelegate();

  CommitId GetId() const { return id_; }

  Status SetValue(const std::string& key,
                  ObjectIdView value,
                  KeyPriority priority);
  Status Delete(const std::string& key);

  Status Commit();
  bool IsCommitted() const;

  Status Rollback();
  bool IsRolledBack() const;

  const std::map<std::string, Entry> GetData() const;

 private:
  const CommitId id_;
  std::map<std::string, Entry> data_;

  bool is_committed_ = false;
  bool is_rolled_back_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeJournalDelegate);
};

}  // namespace fake
}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_DELEGATE_H_
