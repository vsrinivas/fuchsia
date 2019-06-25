// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/journal_entry.h>

#include <memory>

#include <blobfs/writeback-work.h>
#include <zxtest/zxtest.h>

namespace {

using blobfs::JournalEntry;
using blobfs::WritebackWork;

std::unique_ptr<WritebackWork> CreateWork() {
    return std::make_unique<WritebackWork>(nullptr);
}

std::unique_ptr<JournalEntry> CreateEntry(blobfs::JournalWriter* journal) {
    return std::make_unique<JournalEntry>(journal, 0, 1, CreateWork(), false);
}

std::unique_ptr<JournalEntry> CreateSyncEntry(blobfs::JournalWriter* journal) {
    return std::make_unique<JournalEntry>(journal, 0, 0, CreateWork(), false);
}

std::unique_ptr<JournalEntry> CreateDummyEntry(blobfs::JournalWriter* journal) {
    return std::make_unique<JournalEntry>(journal, 0, 0, CreateWork(), true);
}

class FakeJournal : public blobfs::JournalWriter {
  public:
    FakeJournal() {}
    ~FakeJournal() {}

    void ProcessEntryResult(zx_status_t result, JournalEntry* entry) final {
        entry->SetStatusFromResult(result);
    }

    void WriteEntry(JournalEntry* entry) final { write_entry_called_ = true; }
    void DeleteEntry(JournalEntry* entry) final { delete_entry_called_ = true; }

    zx_status_t EnqueueEntryWork(std::unique_ptr<WritebackWork> work) final {
        enqueue_called_ = true;
        // Make the destructor happy.
        work->Transaction().Reset();
        return ZX_OK;
    }

    bool write_entry_called() const { return write_entry_called_; }
    bool delete_entry_called() const { return delete_entry_called_; }
    bool enqueue_called() const { return enqueue_called_; }

  private:
    bool write_entry_called_ = false;
    bool delete_entry_called_ = false;
    bool enqueue_called_ = false;
};

TEST(JournalEntryTest, CreateNormal) {
    FakeJournal journal;
    std::unique_ptr<JournalEntry> entry = CreateEntry(&journal);

    EXPECT_TRUE(entry->HasData());
    EXPECT_FALSE(entry->is_dummy());
}

TEST(JournalEntryTest, CreateSync) {
    FakeJournal journal;
    std::unique_ptr<JournalEntry> entry = CreateSyncEntry(&journal);

    EXPECT_FALSE(entry->HasData());
    EXPECT_FALSE(entry->is_dummy());
}

TEST(JournalEntryTest, CreateDummy) {
    FakeJournal journal;
    std::unique_ptr<JournalEntry> entry = CreateDummyEntry(&journal);

    EXPECT_FALSE(entry->HasData());
    EXPECT_TRUE(entry->is_dummy());
}

TEST(JournalEntryTest, NormalFlow) {
    FakeJournal journal;
    std::unique_ptr<JournalEntry> entry = CreateEntry(&journal);

    EXPECT_OK(entry->GetStatus());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // The first thing to do should be to write the entry data, and the entry
    // should be waiting for completion:
    entry->Start();
    EXPECT_EQ(ZX_ERR_ASYNC, entry->GetStatus());
    EXPECT_TRUE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // When the operation completes, the entry should reflect that:
    entry->SetStatusFromResult(ZX_OK);
    EXPECT_EQ(ZX_OK, entry->GetStatus());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // Moving on should trigger the original request to proceed (data write to
    // the "real" destination, with a call to EnqueueEntryWork):
    EXPECT_EQ(ZX_ERR_ASYNC, entry->Continue());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_TRUE(journal.enqueue_called());

    // When done, say so:
    entry->SetStatusFromResult(ZX_OK);
    EXPECT_EQ(ZX_OK, entry->GetStatus());
    EXPECT_FALSE(journal.delete_entry_called());

    // The final step is to delete the entry:
    EXPECT_EQ(ZX_ERR_STOP, entry->Continue());
    EXPECT_TRUE(journal.delete_entry_called());
}

TEST(JournalEntryTest, NormalFlowWithErrors) {
    FakeJournal journal;
    std::unique_ptr<JournalEntry> entry = CreateEntry(&journal);

    entry->Start();
    EXPECT_EQ(ZX_ERR_ASYNC, entry->GetStatus());

    // When the operation completes, the entry should forward the status.
    entry->SetStatusFromResult(ZX_ERR_IO);
    EXPECT_EQ(ZX_ERR_IO, entry->GetStatus());

    // The caller is free to ignore the error.
    EXPECT_EQ(ZX_ERR_ASYNC, entry->Continue());
    EXPECT_TRUE(journal.enqueue_called());

    // Forward the status again.
    entry->SetStatusFromResult(ZX_ERR_IO);
    EXPECT_EQ(ZX_ERR_IO, entry->GetStatus());
}

TEST(JournalEntryTest, SyncFlow) {
    FakeJournal journal;
    std::unique_ptr<JournalEntry> entry = CreateSyncEntry(&journal);

    EXPECT_OK(entry->GetStatus());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // Does nothing but move along.
    entry->Start();
    EXPECT_EQ(ZX_OK, entry->GetStatus());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // Move along again.
    EXPECT_EQ(ZX_OK, entry->Continue());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // The final step is to release the work item:
    EXPECT_EQ(ZX_ERR_STOP, entry->Continue());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_TRUE(journal.enqueue_called());
}

TEST(JournalEntryTest, DummyFlow) {
    FakeJournal journal;
    std::unique_ptr<JournalEntry> entry = CreateDummyEntry(&journal);

    EXPECT_OK(entry->GetStatus());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // Does nothing but move along.
    entry->Start();
    EXPECT_EQ(ZX_OK, entry->GetStatus());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // Move along again. Note that at this point the journal is supposed to
    // delete the entry instead of keep treating it as a sync entry.
    EXPECT_EQ(ZX_OK, entry->Continue());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_FALSE(journal.enqueue_called());

    // The final step is to release the work item:
    EXPECT_EQ(ZX_ERR_STOP, entry->Continue());
    EXPECT_FALSE(journal.write_entry_called());
    EXPECT_FALSE(journal.delete_entry_called());
    EXPECT_TRUE(journal.enqueue_called());
}

} // namespace
