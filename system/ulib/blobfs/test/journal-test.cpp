// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/journal.h>
#include <unittest/unittest.h>

namespace blobfs {
namespace {

// Mock journal implementation which can be used to test JournalEntry / JournalProcessor
// functionality.
class MockJournal : public JournalBase {
public:
    MockJournal() : readonly_(false), capacity_(0) {}

    void SendSignal(zx_status_t status) final {
        if (status != ZX_OK) {
            readonly_ = true;
        }
    }

    fbl::unique_ptr<blobfs::WritebackWork> CreateDefaultWork() {
        return CreateWork();
    }

    fbl::unique_ptr<WritebackWork> CreateBufferedWork(size_t block_count) {
        fbl::unique_ptr<WritebackWork> work = CreateWork();

        zx::vmo vmo;
        ZX_ASSERT(zx::vmo::create(PAGE_SIZE, 0, &vmo) == ZX_OK);
        work->Enqueue(vmo, 0, 0, block_count);
        work->SetBuffer(2);
        return work;
    }

private:
    size_t GetCapacity() const final {
        return capacity_;
    }

    bool IsReadOnly() const final {
        return readonly_;
    }

    fbl::unique_ptr<WritebackWork> CreateWork() final {
        return fbl::make_unique<blobfs::WritebackWork>(nullptr, nullptr);
    }

    // The following functions are no-ops, and only exist so they can be called by the
    // JournalProcessor.
    void PrepareBuffer(JournalEntry* entry) final {}
    void PrepareDelete(JournalEntry* entry, WritebackWork* work) final {}
    zx_status_t EnqueueEntryWork(fbl::unique_ptr<WritebackWork> work) final {
        return ZX_OK;
    }

    bool readonly_;
    size_t capacity_;
};

static bool JournalEntryLifetimeTest() {
    BEGIN_TEST;

    // Create a dummy journal and journal processor.
    MockJournal journal;
    blobfs::JournalProcessor processor(&journal);

    // Create and process a 'work' entry.
    fbl::unique_ptr<blobfs::JournalEntry> entry(
        new blobfs::JournalEntry(&journal, blobfs::EntryStatus::kInit, 0, 0,
                                 journal.CreateBufferedWork(1)));
    fbl::unique_ptr<blobfs::WritebackWork> first_work = journal.CreateDefaultWork();
    first_work->SetSyncCallback(entry->CreateSyncCallback());
    processor.ProcessWorkEntry(std::move(entry));

    // Create and process another 'work' entry.
    entry.reset(new blobfs::JournalEntry(&journal, blobfs::EntryStatus::kInit, 0, 0,
                                         journal.CreateBufferedWork(1)));
    fbl::unique_ptr<blobfs::WritebackWork> second_work = journal.CreateDefaultWork();
    second_work->SetSyncCallback(entry->CreateSyncCallback());
    processor.ProcessWorkEntry(std::move(entry));

    // Enqueue the processor's work (this is a no-op).
    processor.EnqueueWork();

    // Simulate an error in the writeback thread by calling the first entry's callback with an
    // error status.
    first_work->Reset(ZX_ERR_BAD_STATE);

    // Process the wait queue.
    processor.ProcessWaitQueue();

    // Now, attempt to call the second entry's callback with the error. If we are incorrectly
    // disposing of entries before their callbacks have been invoked, this should trigger a
    // "use-after-free" asan error, since the JournalEntry referenced by second_work will have
    // already been deleted (see ZX-2940).
    second_work->Reset(ZX_ERR_BAD_STATE);

    // Additionally, we should check that the processor queues are not empty - i.e., there is still
    // one entry waiting to be processed.
    ASSERT_FALSE(processor.IsEmpty());

    // Process the rest of the queues.
    processor.ProcessWaitQueue();
    processor.ProcessDeleteQueue();
    processor.ProcessSyncQueue();

    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsJournalTests)
RUN_TEST(blobfs::JournalEntryLifetimeTest)
END_TEST_CASE(blobfsJournalTests);
