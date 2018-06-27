// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_H_

#include <memory>
#include <string>

#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/storage/fake/fake_journal_delegate.h"
#include "peridot/bin/ledger/storage/public/journal.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {
namespace fake {

// A |FakeJournal| is an in-memory journal.
class FakeJournal : public Journal {
 public:
  explicit FakeJournal(FakeJournalDelegate* delegate);
  ~FakeJournal() override;

  void Commit(
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  Status Rollback();

  // Journal:
  void Put(convert::ExtendedStringView key, ObjectIdentifier object_identifier,
           KeyPriority priority, fit::function<void(Status)> callback) override;
  void Delete(convert::ExtendedStringView key,
              fit::function<void(Status)> callback) override;
  const JournalId& GetId() const override;

 private:
  FakeJournalDelegate* delegate_;
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeJournal);
};

}  // namespace fake
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_JOURNAL_H_
