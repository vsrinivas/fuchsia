// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_H_
#define APPS_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_H_

#include <memory>
#include <string>

#include "apps/ledger/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/storage/public/journal.h"
#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {
namespace fake {

// A |FakeJournal| is an in-memory journal.
class FakeJournal : public Journal {
 public:
  explicit FakeJournal(FakeJournalDelegate* delegate);
  ~FakeJournal() override;

  // Journal:
  Status Put(const std::string& key,
             ObjectIdView blob_id,
             KeyPriority priority) override;
  Status Delete(const std::string& key) override;
  Status Commit(CommitId* commit_id) override;
  Status Rollback() override;

 private:
  FakeJournalDelegate* delegate_;
  FTL_DISALLOW_COPY_AND_ASSIGN(FakeJournal);
};

}  // namespace fake
}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_H_
