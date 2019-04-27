// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/journal.h>
#include <unittest/unittest.h>

namespace blobfs {
namespace {

// Mock journal implementation which can be used to test JournalEntry / JournalProcessor
// functionality.
class FakeJournal : public JournalBase {
public:
    FakeJournal() : readonly_(false), capacity_(0) {}

    ~FakeJournal() {
        // On destruction, clean up work_queue_ entries.
        while (!work_queue_.is_empty()) {
            work_queue_.front().MarkCompleted(ZX_OK);
            work_queue_.pop();
        }
    }

    void ProcessEntryResult(zx_status_t result, JournalEntry* entry) final {
        entry->SetStatusFromResult(result);
        if (result != ZX_OK) {
            readonly_ = true;
        }
    }

    fbl::unique_ptr<WritebackWork> CreateDefaultWork() {
        return CreateWork();
    }

    fbl::unique_ptr<WritebackWork> CreateBufferedWork(size_t block_count) {
        fbl::unique_ptr<WritebackWork> work = CreateWork();

        zx::vmo vmo;
        ZX_ASSERT(zx::vmo::create(PAGE_SIZE, 0, &vmo) == ZX_OK);
        work->Transaction().Enqueue(vmo, 0, 0, block_count);
        work->Transaction().SetBuffer(2);
        return work;
    }

    fbl::unique_ptr<WritebackWork> DequeueWork() {
        return work_queue_.pop();
    }

private:
    using WorkQueue = fs::Queue<fbl::unique_ptr<WritebackWork>>;

    size_t GetCapacity() const final {
        return capacity_;
    }

    bool IsReadOnly() const final {
        return readonly_;
    }

    fbl::unique_ptr<WritebackWork> CreateWork() final {
        return std::make_unique<WritebackWork>(nullptr);
    }

    // The following functions are no-ops, and only exist so they can be called by the
    // JournalProcessor.
    void PrepareBuffer(JournalEntry* entry) final {}
    void PrepareDelete(JournalEntry* entry, WritebackWork* work) final {}

    // Stores the WritebackWork in work_queue_;
    zx_status_t EnqueueEntryWork(fbl::unique_ptr<WritebackWork> work) final {
        work_queue_.push(std::move(work));
        return ZX_OK;
    }

    bool readonly_;
    size_t capacity_;

    // Enqueued entry works are stored here.
    WorkQueue work_queue_;
};

static bool JournalEntryLifetimeTest() {
    BEGIN_TEST;

    // Create a dummy journal and journal processor.
    FakeJournal journal;
    JournalProcessor processor(&journal);

    // Create and process a 'work' entry.
    fbl::unique_ptr<JournalEntry> entry(
        new JournalEntry(&journal, EntryStatus::kInit, 0, 0,
                         journal.CreateBufferedWork(1)));
    fbl::unique_ptr<WritebackWork> first_work = journal.CreateDefaultWork();
    first_work->SetSyncCallback(entry->CreateSyncCallback());
    processor.ProcessWorkEntry(std::move(entry));

    // Create and process another 'work' entry.
    entry.reset(new JournalEntry(&journal, EntryStatus::kInit, 0, 0,
                                 journal.CreateBufferedWork(1)));
    fbl::unique_ptr<WritebackWork> second_work = journal.CreateDefaultWork();
    second_work->SetSyncCallback(entry->CreateSyncCallback());
    processor.ProcessWorkEntry(std::move(entry));

    // Enqueue the processor's work (this is a no-op).
    processor.EnqueueWork();

    // Simulate an error in the writeback thread by calling the first entry's callback with an
    // error status.
    first_work->MarkCompleted(ZX_ERR_BAD_STATE);

    // Process the wait queue.
    processor.ProcessWaitQueue();

    // Now, attempt to call the second entry's callback with the error. If we are incorrectly
    // disposing of entries before their callbacks have been invoked, this should trigger a
    // "use-after-free" asan error, since the JournalEntry referenced by second_work will have
    // already been deleted (see ZX-2940).
    second_work->MarkCompleted(ZX_ERR_BAD_STATE);

    // Additionally, we should check that the processor queues are not empty - i.e., there is still
    // one entry waiting to be processed.
    ASSERT_FALSE(processor.IsEmpty());

    // Process the rest of the queues.
    processor.ProcessWaitQueue();
    processor.ProcessDeleteQueue();
    processor.ProcessSyncQueue();

    END_TEST;
}

static bool JournalProcessorResetWorkTest() {
    BEGIN_TEST;
    // Create a dummy journal and journal processor.
    FakeJournal journal;
    JournalProcessor processor(&journal);

    // Create and process a 'work' entry.
    fbl::unique_ptr<JournalEntry> entry(
        new JournalEntry(&journal, EntryStatus::kInit, 0, 1, journal.CreateBufferedWork(1)));
    fbl::unique_ptr<WritebackWork> first_work = journal.CreateDefaultWork();
    first_work->SetSyncCallback(entry->CreateSyncCallback());
    processor.ProcessWorkEntry(std::move(entry));

    // Create and process another 'work' entry.
    entry.reset(new JournalEntry(&journal, EntryStatus::kInit, 2, 3,
                                 journal.CreateBufferedWork(1)));
    fbl::unique_ptr<WritebackWork> second_work = journal.CreateDefaultWork();
    second_work->SetSyncCallback(entry->CreateSyncCallback());
    processor.ProcessWorkEntry(std::move(entry));

    // Enqueue and process the processor's work.
    processor.EnqueueWork();
    journal.DequeueWork()->MarkCompleted(ZX_OK);

    // Call the entries' callbacks so they are moved to the next queue.
    first_work->MarkCompleted(ZX_OK);
    second_work->MarkCompleted(ZX_OK);

    // Process the wait queue.
    processor.ProcessWaitQueue();

    // Grab the works that were enqueued from the two entries in the wait queue (which have now
    // been moved to the delete queue).
    first_work = journal.DequeueWork();
    second_work = journal.DequeueWork();

    ASSERT_NE(first_work.get(), nullptr);
    ASSERT_NE(second_work.get(), nullptr);

    // Simulate an error in the writeback thread by calling the second entry's callback with an
    // error status.
    first_work->MarkCompleted(ZX_OK);
    second_work->MarkCompleted(ZX_ERR_BAD_STATE);

    processor.ProcessDeleteQueue();

    ASSERT_TRUE(processor.HasError());
    ASSERT_GT(processor.GetBlocksProcessed(), 0);

    // Since we encountered an error and blocks have been processed, we must reset the work
    // generated by the processor. Previously, since ResetWork() would invoke the WritebackWork
    // callback but would not delete the WritebackWork, this would trigger an assertion (work_ ==
    // nullptr) when switching to the kSync context.
    processor.ResetWork();

    processor.ProcessSyncQueue();
    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsJournalTests)
RUN_TEST(blobfs::JournalEntryLifetimeTest)
RUN_TEST(blobfs::JournalProcessorResetWorkTest)
END_TEST_CASE(blobfsJournalTests)
