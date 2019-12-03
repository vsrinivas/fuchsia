// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_JOURNAL_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_JOURNAL_H_

#include <lib/fit/function.h>

#include <memory>
#include <string>

#include "src/ledger/bin/storage/fake/fake_journal_delegate.h"
#include "src/ledger/bin/storage/public/journal.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {
namespace fake {

// A |FakeJournal| is an in-memory journal.
class FakeJournal : public Journal {
 public:
  explicit FakeJournal(FakeJournalDelegate* delegate);
  FakeJournal(const FakeJournal&) = delete;
  FakeJournal& operator=(const FakeJournal&) = delete;
  ~FakeJournal() override;

  void Commit(fit::function<void(Status, std::unique_ptr<const storage::Commit>)> callback);

  // Journal:
  void Put(convert::ExtendedStringView key, ObjectIdentifier object_identifier,
           KeyPriority priority) override;
  void Delete(convert::ExtendedStringView key) override;
  void Clear() override;

 private:
  FakeJournalDelegate* delegate_;
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_JOURNAL_H_
