// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/journal.h>

#include <fbl/unique_ptr.h>
#include <lib/cksum.h>
#include <zircon/types.h>

namespace blobfs {

// TODO(ZX-2415): Add tracing/metrics collection to journal related operations.

// Thread which asynchronously processes journal entries.
static int JournalThread(void* arg) {
    Journal* journal = reinterpret_cast<Journal*>(arg);
    journal->ProcessLoop();
    return 0;
}

JournalEntry::JournalEntry(Journal* journal, EntryStatus status, size_t header_index,
                           size_t commit_index, fbl::unique_ptr<WritebackWork> work)
        : journal_(journal), status_(static_cast<uint32_t>(status)), block_count_(0),
          header_index_(header_index), commit_index_(commit_index), work_(fbl::move(work)) {
    if (status != EntryStatus::kInit) {
        // In the case of a sync request or error, return early.
        ZX_DEBUG_ASSERT(status == EntryStatus::kSync || status == EntryStatus::kError);
        return;
    }

    size_t work_blocks = work_->BlkCount();
    // Ensure the work is valid.
    ZX_DEBUG_ASSERT(work_blocks > 0);
    ZX_DEBUG_ASSERT(work_->IsBuffered());
    ZX_DEBUG_ASSERT(work_blocks <= kMaxEntryDataBlocks);

    // Copy all target blocks from the WritebackWork to the entry's header block.
    for (size_t i = 0; i < work_->Requests().size(); i++) {
        WriteRequest& request = work_->Requests()[i];
        for (size_t j = request.dev_offset; j < request.dev_offset + request.length; j++) {
            header_block_.target_blocks[block_count_++] = j;
        }
    }

    ZX_DEBUG_ASSERT(work_blocks == block_count_);

    // Set other information in the header/commit blocks.
    header_block_.magic = kEntryHeaderMagic;
    header_block_.num_blocks = block_count_;
    header_block_.timestamp = zx_ticks_get();
    commit_block_.magic = kEntryCommitMagic;
    commit_block_.timestamp = header_block_.timestamp;
    commit_block_.checksum = 0;
}

fbl::unique_ptr<WritebackWork> JournalEntry::TakeWork() {
    ZX_DEBUG_ASSERT(work_ != nullptr);

    if (header_index_ != commit_index_) {
        // If the journal entry contains any transactions, set the work closure to update the entry
        // status on write completion. This currently assumes that a WritebackWork with associated
        // transactions will NOT already have a closure attached. If we ever want to include
        // transactions on a syncing WritebackWork, we will need to revisit this.
        work_->SetSyncCallback(CreateSyncCallback());
    }

    return fbl::move(work_);
}

ReadyCallback JournalEntry::CreateReadyCallback() {
    return [this] () {
        // If the entry is in a waiting state, it is ready to be written to disk.
        return GetStatus() == EntryStatus::kWaiting;
    };
}

SyncCallback JournalEntry::CreateSyncCallback() {
    return [this] (zx_status_t status) {
        EntryStatus last_status;
        // The callback sets the state of the JournalEntry based on the status of writeback.
        if (status == ZX_OK) {
            last_status = SetStatus(EntryStatus::kPersisted);
            ZX_DEBUG_ASSERT(last_status == EntryStatus::kWaiting);
        } else {
            SetStatus(EntryStatus::kError);
        }

        // Signal the journal that an entry is complete and ready for processing.
        journal_->SendSignal(status);
    };
}

void JournalEntry::SetChecksum(uint32_t checksum) {
    commit_block_.checksum = checksum;
}

zx_status_t Journal::Create(Blobfs* blobfs, uint64_t journal_blocks, uint64_t start_block,
                            fbl::unique_ptr<Journal>* out) {
    // Create the buffer with 1 less than total journal blocks.
    // (1 block must be reserved for journal info).
    zx_status_t status;
    fbl::unique_ptr<Buffer> buffer;
    if ((status = Buffer::Create(blobfs, journal_blocks - 1, "blobfs-journal", &buffer)) != ZX_OK) {
        return status;
    }

    // Create another buffer for the journal info block.
    fbl::unique_ptr<Buffer> info;
    if ((status = Buffer::Create(blobfs, 1, "blobfs-journal-info", &info)) != ZX_OK) {
        return status;
    }

    // Reserve the only block in the info buffer so its impossible to copy transactions to it.
    info->ReserveIndex();

    // Create the Journal with the newly created vmos.
    fbl::unique_ptr<Journal> journal(new Journal(blobfs, fbl::move(info), fbl::move(buffer),
                                                 start_block));

    // Load contents of journal from disk.
    if ((status = journal->Load()) != ZX_OK) {
        fprintf(stderr, "Journal: Failed to load from disk: %d\n", status);
        return status;
    }

    *out = fbl::move(journal);
    return ZX_OK;
}

Journal::~Journal() {
    WritebackState state;

    {
        fbl::AutoLock lock(&lock_);
        state = state_;

        // Signal the background thread.
        unmounting_ = true;
        cnd_signal(&consumer_cvar_);
    }

    if (state != WritebackState::kInit && state != WritebackState::kReady) {
        // Block until the thread completes itself.
        thrd_join(thread_, nullptr);
    }

    // Ensure that work and producer queues are currently empty.
    ZX_DEBUG_ASSERT(work_queue_.is_empty());
    ZX_DEBUG_ASSERT(producer_queue_.is_empty());
}

zx_status_t Journal::Load() {
    fbl::AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(state_ == WritebackState::kInit);

    // Load info block and journal entries into their respective buffers.
    fs::ReadTxn txn(blobfs_);
    info_->Load(&txn, start_block_);
    entries_->Load(&txn, start_block_ + 1);
    zx_status_t status = txn.Transact();

    if (status != ZX_OK) {
        return status;
    }

    JournalInfo* info = GetInfo();

    // Verify the journal magic matches.
    if (info->magic != kJournalMagic) {
        fprintf(stderr, "Journal info bad magic\n");
        return ZX_ERR_BAD_STATE;
    }

    if (info->start_block > 0 || info->num_blocks > 0 || info->timestamp > 0) {
        // Who checks the checksum? (It's us. We are doing it.)
        uint32_t old_checksum = info->checksum;
        info->checksum = 0;

        uint8_t* info_ptr = reinterpret_cast<uint8_t*>(info);
        info->checksum = crc32(0, info_ptr, sizeof(JournalInfo));

        if (old_checksum != info->checksum) {
            fprintf(stderr, "Journal info checksum corrupt\n");
            return ZX_ERR_BAD_STATE;
        }
    }

    return status;
}

zx_status_t Journal::Replay() {
    fbl::AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(state_ == WritebackState::kInit);

    uint64_t timestamp = 0;
    size_t start = GetInfo()->start_block;
    size_t length = GetInfo()->num_blocks;
    size_t total_entries = 0;
    size_t total_blocks = 0;

    // Replay entries until we find one that isn't valid.
    while (true) {
        uint64_t entry_blocks;
        // |start| is the header index of the next entry.
        zx_status_t status = ReplayEntry(start, length, &entry_blocks, &timestamp);
        if (status == ZX_ERR_OUT_OF_RANGE) {
            break;
        } else if (status != ZX_OK) {
            return status;
        }

        total_entries++;
        total_blocks += entry_blocks;

        start += entry_blocks;
        start %= entries_->capacity();

        if (length) {
            length -= entry_blocks;
        }
    }

    // TODO(planders): Sync to ensure that all entries have been written out before resetting the
    //                 on-disk state of the journal.
    if (total_entries > 0) {
        printf("Found and replayed %zu total blobfs journal entries starting from index %zu, "
               "including %zu total blocks.\n",
               total_entries, GetInfo()->start_block, total_blocks);
    } else if (start == 0 && length == 0) {
        // If no entries were found and journal is already in its default state,
        // return without writing out any changes.
        state_ = WritebackState::kReady;
        return ZX_OK;
    }

    // We expect length to be 0 at this point, assuming the journal was not corrupted and replay
    // completed successfully. However, in the case of corruption of the journal this may not be the
    // case. Since we cannot currently recover from this situation we should proceed as normal.
    zx_status_t status = CommitReplay();
    if (status != ZX_OK) {
        return status;
    }

    // Now that we've resolved any remaining entries, we are ready to start journal writeback.
    state_ = WritebackState::kReady;
    return ZX_OK;
}

zx_status_t Journal::InitWriteback() {
    fbl::AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(state_ == WritebackState::kReady);

    if (entries_->start() > 0 || entries_->length() > 0) {
        fprintf(stderr, "Cannot initialize journal writeback - entries may still exist.\n");
        return ZX_ERR_BAD_STATE;
    }

    if (thrd_create_with_name(&thread_, JournalThread, this, "blobfs-journal") !=
        thrd_success) {
        fprintf(stderr, "Failed to create journal thread.\n");
        return ZX_ERR_NO_RESOURCES;
    }

    return ZX_OK;
}

zx_status_t Journal::Enqueue(fbl::unique_ptr<WritebackWork> work) {
    // Verify that the work exists and has not already been prepared for writeback.
    ZX_DEBUG_ASSERT(work != nullptr);
    ZX_DEBUG_ASSERT(!work->IsBuffered());

    // Block count will be the number of blocks in the transaction + header + commit.
    size_t blocks = work->BlkCount();
    // By default set the header/commit indices to the buffer capacity,
    // since this will be an invalid index value.
    size_t header_index = entries_->capacity();
    size_t commit_index = entries_->capacity();

    fbl::AutoLock lock(&lock_);

    zx_status_t status = ZX_OK;
    if (IsReadOnly()) {
        // If we are in "read only" mode, set an error status.
        status = ZX_ERR_BAD_STATE;
    } else if (blocks) {
        // If the work contains no blocks (i.e. it is a sync work), proceed to create an entry
        // without enqueueing any data to the buffer.

        // Add 2 blocks to the block count for the journal entry's header/commit blocks.
        blocks += 2;

        // Ensure we have enough space to write the current entry to the buffer.
        // If not, wait until space becomes available.
        EnsureSpaceLocked(blocks);

        if (IsReadOnly()) {
            // The Journal is in a bad state and is no longer accepting new entries.
            ZX_ASSERT_MSG(status == ZX_ERR_BAD_STATE,
                          "Requested txn (%zu blocks) larger than journal buffer", blocks);
        } else {
            // Assign header index of journal entry to the next available value before we attempt to
            // copy the meat of the entry to the buffer.
            header_index = entries_->ReserveIndex();

            // Copy the data from WritebackWork to the journal buffer. We can wait to write out the
            // header and commit blocks asynchronously, since this will involve calculating the
            // checksum.
            // TODO(planders): Release the lock while transaction is being copied.
            entries_->CopyTransaction(work.get());

            // Assign commit_index immediately after copying to the buffer.
            // Increase length_ accordingly.
            commit_index = entries_->ReserveIndex();

            // Make sure that commit index matches what we expect
            // based on header index, block count, and buffer size.
            ZX_DEBUG_ASSERT(commit_index == (header_index + blocks - 1) % entries_->capacity());
        }
    }

    // Create the journal entry and push it onto the work queue.
    fbl::unique_ptr<JournalEntry> entry = CreateEntry(header_index, commit_index, fbl::move(work));

    if (entry->GetStatus() == EntryStatus::kInit) {
        // If we have a non-sync work, there is some extra preparation we need to do.
        if (status == ZX_OK) {
            // Prepare a WritebackWork to write out the entry to disk. Note that this does not
            // fully prepare the buffer for writeback, so a ready callback is added to the work as
            // part of this step.
            PrepareWork(entry.get(), &work);
            ZX_DEBUG_ASSERT(work != nullptr);
            status = EnqueueEntryWork(fbl::move(work));
        } else {
            // If the status is not okay (i.e. we are in a readonly state), do no additional
            // processing but set the entry state to error.
            entry->SetStatus(EntryStatus::kError);
        }
    }

    // Queue the entry to be processed asynchronously.
    work_queue_.push(fbl::move(entry));

    // Signal the JournalThread that there is at least one entry ready to be processed.
    SendSignalLocked(status);
    return status;
}

void Journal::SendSignalLocked(zx_status_t status) {
    if (status == ZX_OK) {
        // Once writeback has entered a read only state, no further transactions should succeed.
        ZX_ASSERT(state_ != WritebackState::kReadOnly);
    } else {
        state_ = WritebackState::kReadOnly;
    }
    consumer_signalled_ = true;
    cnd_signal(&consumer_cvar_);
}

fbl::unique_ptr<JournalEntry> Journal::CreateEntry(uint64_t header_index, uint64_t commit_index,
                                                   fbl::unique_ptr<WritebackWork> work) {
    EntryStatus status = EntryStatus::kInit;

    if (work->BlkCount() == 0) {
        // If the work has no transactions, this is a sync work - we can return early.
        // Right now we make the assumption that if a WritebackWork has any transactions, it cannot
        // have a corresponding sync callback. We may need to revisit this later.
        status = EntryStatus::kSync;
    } else if (IsReadOnly()) {
        // If the journal is in a read only state, set the entry status to error.
        status = EntryStatus::kError;
    }

    return fbl::make_unique<JournalEntry>(this, status, header_index, commit_index,
                                          fbl::move(work));
}

void Journal::PrepareWork(JournalEntry* entry, fbl::unique_ptr<WritebackWork>* out) {
    size_t header_index = entry->GetHeaderIndex();
    size_t commit_index = entry->GetCommitIndex();
    size_t block_count = entry->BlockCount();

    if (block_count == 0) {
        // If journal entry has size 0, it is an empty sync entry, and we don't need to write
        // anything to the journal.
        ZX_DEBUG_ASSERT(header_index == entries_->capacity());
        ZX_DEBUG_ASSERT(commit_index == entries_->capacity());
        return;
    }

    fbl::unique_ptr<WritebackWork> work = CreateWork();

    // Update work with transactions for the current entry.
    AddEntryTransaction(header_index, block_count, work.get());

    // Make sure the work is prepared for the writeback queue.
    work->SetReadyCallback(entry->CreateReadyCallback());
    work->SetSyncCallback(entry->CreateSyncCallback());
    *out = fbl::move(work);
}

void Journal::PrepareBuffer(JournalEntry* entry) {
    size_t header_index = entry->GetHeaderIndex();
    size_t commit_index = entry->GetCommitIndex();
    size_t block_count = entry->BlockCount();

    if (block_count == 0) {
        // If journal entry has size 0, it is an empty sync entry, and we don't need to write
        // anything to the journal.
        ZX_DEBUG_ASSERT(header_index == entries_->capacity());
        ZX_DEBUG_ASSERT(commit_index == entries_->capacity());
        return;
    }

    // Copy header block of the journal entry into the journal buffer. We must write the header
    // block into the buffer before the commit block so we can generate the checksum.
    void* data = entries_->MutableData(header_index);
    memset(data, 0, kBlobfsBlockSize);
    memcpy(data, &entry->GetHeaderBlock(), sizeof(HeaderBlock));

    // Now that the header block has been written to the buffer, we can calculate a checksum for
    // the header + all journaled metadata blocks and set it in the entry's commit block.
    entry->SetChecksum(GenerateChecksum(header_index, commit_index));

    // Write the commit block (now with checksum) to the journal buffer.
    data = entries_->MutableData(commit_index);
    memset(data, 0, kBlobfsBlockSize);
    memcpy(data, &entry->GetCommitBlock(), sizeof(CommitBlock));
}

void Journal::PrepareDelete(JournalEntry* entry, WritebackWork* work) {
    ZX_DEBUG_ASSERT(work != nullptr);
    size_t header_index = entry->GetHeaderIndex();
    size_t commit_index = entry->GetCommitIndex();
    size_t block_count = entry->BlockCount();

    if (block_count == 0) {
        // If journal entry has size 0, it is an empty sync entry, and we don't need to write
        // anything to the journal.
        ZX_DEBUG_ASSERT(header_index == entries_->capacity());
        ZX_DEBUG_ASSERT(commit_index == entries_->capacity());
        return;
    }

    // Overwrite the header & commit block in the buffer with empty data.
    memset(entries_->MutableData(header_index), 0, kBlobfsBlockSize);
    memset(entries_->MutableData(commit_index), 0, kBlobfsBlockSize);

    // Enqueue transactions for the header/commit blocks.
    entries_->AddTransaction(header_index, start_block_ + 1 + header_index, 1, work);
    entries_->AddTransaction(commit_index, start_block_ + 1 + commit_index, 1, work);
}

fbl::unique_ptr<WritebackWork> Journal::CreateWork() {
    fbl::unique_ptr<WritebackWork> work;
    blobfs_->CreateWork(&work, nullptr);
    ZX_DEBUG_ASSERT(work != nullptr);
    return fbl::move(work);
}

zx_status_t Journal::EnqueueEntryWork(fbl::unique_ptr<WritebackWork> work) {
    entries_->ValidateTransaction(work.get());
    return blobfs_->EnqueueWork(fbl::move(work), EnqueueType::kData);
}

bool Journal::VerifyEntryMetadata(size_t header_index, uint64_t last_timestamp, bool expect_valid) {
    HeaderBlock* header = GetHeaderBlock(header_index);
    // If length_ > 0, the next entry should be guaranteed.
    if (header->magic != kEntryHeaderMagic || header->timestamp <= last_timestamp) {
        // If the next calculated header block is either 1) not a header block, or 2) does not
        // have a timestamp strictly later than the previous entry, it is not a valid entry and
        // should not be replayed. This is only a journal replay "error" if, according to the
        // journal super block, we still have some entries left to process (i.e. length_ > 0).
        if (expect_valid) {
            fprintf(stderr, "Journal Replay Error: invalid header found.\n");
        }

        return false;
    }

    size_t commit_index = (header_index + header->num_blocks + 1) % entries_->capacity();
    CommitBlock* commit = GetCommitBlock(commit_index);

    if (commit->magic != kEntryCommitMagic) {
        fprintf(stderr, "Journal Replay Error: commit magic does not match expected\n");
        return false;
    }

    if (commit->timestamp != header->timestamp) {
        fprintf(stderr, "Journal Replay Error: commit timestamp does not match expected\n");
        return false;
    }

    // Calculate the checksum of the entry data to verify the commit block's checksum.
    uint32_t checksum = GenerateChecksum(header_index, commit_index);

    // Since we already found a valid header, we expect this to be a valid entry. If something
    // in the commit block does not match what we expect, this is an error.
    if (commit->checksum != checksum) {
        fprintf(stderr, "Journal Replay Error: commit checksum does not match expected\n");
        return false;
    }

    return true;
}

zx_status_t Journal::ReplayEntry(size_t header_index, size_t remaining_length,
                                 uint64_t* entry_blocks, uint64_t* timestamp) {
    ZX_DEBUG_ASSERT(state_ == WritebackState::kInit);

    bool expect_valid = remaining_length > 0;
    if (!VerifyEntryMetadata(header_index, *timestamp, expect_valid)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    HeaderBlock* header = GetHeaderBlock(header_index);
    *timestamp = header->timestamp;
    *entry_blocks = header->num_blocks + 2;
    // We have found a valid entry - ensure that remaining_length is valid
    // (either 0 remaining, or enough to fit this entry).
    ZX_DEBUG_ASSERT(remaining_length == 0 || remaining_length >= *entry_blocks);

    fbl::unique_ptr<WritebackWork> work = CreateWork();

    // Enqueue one block at a time, since they may not end up being contiguous on disk.
    for (unsigned i = 0; i < header->num_blocks; i++) {
        size_t vmo_block = (header_index + i + 1) % entries_->capacity();
        entries_->AddTransaction(vmo_block, header->target_blocks[i], 1, work.get());
    }

    // Replay (and therefore mount) will fail if we cannot enqueue the replay work. Since the
    // journal itself is not corrupt (at least up to this point), we would expect replay to
    // succeed on a subsequent attempt, so we should keep any existing entries intact. (i.e.,
    // do not reset the journal metadata in this failure case).
    zx_status_t status = EnqueueEntryWork(fbl::move(work));
    if (status != ZX_OK) {
        fprintf(stderr, "Journal replay failed with status %d\n", status);
    }

    return status;
}

zx_status_t Journal::CommitReplay() {
    ZX_DEBUG_ASSERT(state_ == WritebackState::kInit);

    // Overwrite the first journal entry block to 0. Since we are resetting the info block to point
    // to 0 as the first entry, we expect that block 0 will not contain a valid entry. Overwriting
    // it will ensure that this is not the case.
    memset(entries_->MutableData(0), 0, kBlobfsBlockSize);
    fbl::unique_ptr<WritebackWork> work = CreateWork();

    entries_->AddTransaction(0, start_block_ + 1, 1, work.get());

    zx_status_t status;
    if ((status = EnqueueEntryWork(fbl::move(work))) != ZX_OK) {
        fprintf(stderr, "Journal replay failed with status %d\n", status);
        return status;
    }

    // Write out the updated info block to disk.
    if ((status = WriteInfo(entries_->start(), entries_->length())) != ZX_OK) {
        fprintf(stderr, "Journal replay failed with status %d\n", status);
        return status;
    }

    // Wait for any replayed entries to complete before completing replay.
    work = CreateWork();
    sync_completion_t completion;
    sync_completion_reset(&completion);

    work->SetSyncCallback([&completion, &status](zx_status_t new_status) {
        status = new_status;
        sync_completion_signal(&completion);
    });

    if ((status = EnqueueEntryWork(fbl::move(work))) != ZX_OK) {
        fprintf(stderr, "Journal replay failed with status %d\n", status);
        return status;
    }

    sync_completion_wait(&completion, ZX_TIME_INFINITE);

    // Return a successful status, even if we detected corrupt metadata or entries.
    // Our metadata should still be in a consistent state so it will be safe to mount regardless.
    return ZX_OK;
}

zx_status_t Journal::WriteInfo(uint64_t start, uint64_t length) {
    JournalInfo* info = GetInfo();

    if (start == info->start_block && length == info->num_blocks) {
        // If the current buffer start/len match the info block, skip the writing step.
        return ZX_OK;
    }

    fbl::unique_ptr<WritebackWork> work;
    blobfs_->CreateWork(&work, nullptr);

    info->start_block = start;
    info->num_blocks = length;
    info->timestamp = zx_ticks_get();

    // Set the checksum to 0 so we can calculate the checksum of the rest of the info block.
    info->checksum = 0;
    uint8_t* info_ptr = reinterpret_cast<uint8_t*>(info);
    info->checksum = crc32(0, info_ptr, sizeof(JournalInfo));

    info_->AddTransaction(0, start_block_, 1, work.get());
    info_->ValidateTransaction(work.get());
    return blobfs_->EnqueueWork(fbl::move(work), EnqueueType::kData);
}

void Journal::EnsureSpaceLocked(size_t blocks) {
    while (!entries_->IsSpaceAvailable(blocks)) {
        // Not enough room to write back work, yet. Wait until room is available.
        Waiter w;
        producer_queue_.push(&w);

        do {
            cnd_wait(&producer_cvar_, lock_.GetInternal());
        } while ((&producer_queue_.front() != &w) && // We are first in line to enqueue...
                (!entries_->IsSpaceAvailable(blocks))); // ... and there is enough space for us.

        producer_queue_.pop();
    }
}

void Journal::AddEntryTransaction(size_t start, size_t length, WritebackWork* work) {
    // Ensure the request fits within the buffer.
    ZX_DEBUG_ASSERT(start < entries_->capacity());
    ZX_DEBUG_ASSERT(length > 0);
    ZX_DEBUG_ASSERT(length < entries_->capacity());
    ZX_DEBUG_ASSERT(work != nullptr);

    // Adjust the length of the first transaction in
    // case it wraps around to the front of the buffer.
    size_t first_length = length;

    if (start + length > entries_->capacity()) {
        first_length = entries_->capacity() - start;
    }

    // Ensure we do not have an empty transaction.
    ZX_DEBUG_ASSERT(first_length > 0);

    // Enqueue the first part of the transaction.
    size_t disk_start = start_block_ + 1;
    entries_->AddTransaction(start, disk_start + start, first_length, work);

    // If we wrapped around to the front of the journal,
    // enqueue a second transaction with the remaining data + commit block.
    if (first_length < length) {
        entries_->AddTransaction(0, disk_start, length - first_length, work);
    }
}

uint32_t Journal::GenerateChecksum(size_t header_index, size_t commit_index) {
    ZX_DEBUG_ASSERT(commit_index != header_index);

    size_t first_length = 0;

    // Determine how long the first part of the transaction is.
    if (commit_index < header_index) {
        first_length = entries_->capacity() - header_index;
    } else {
        first_length = commit_index - header_index;
    }

    ZX_DEBUG_ASSERT(first_length > 0);

    // Calculate checksum.
    uint8_t* data_ptr = static_cast<uint8_t*>(entries_->MutableData(header_index));
    uint32_t checksum = crc32(0, data_ptr, first_length * kBlobfsBlockSize);

    // If the transaction wraps around the buffer, update checksum for the second half.
    if (commit_index < header_index) {
        data_ptr = static_cast<uint8_t*>(entries_->MutableData(0));
        checksum = crc32(checksum, data_ptr, commit_index * kBlobfsBlockSize);
    }

    return checksum;
}

fbl::unique_ptr<JournalEntry> Journal::GetNextEntry() {
    fbl::AutoLock lock(&lock_);
    return work_queue_.pop();
}

void Journal::ProcessQueues(JournalProcessor* processor) {
    // Process all entries in the work queue.
    fbl::unique_ptr<JournalEntry> entry;
    while ((entry = GetNextEntry()) != nullptr) {
        // TODO(planders): For each entry that we process, we can potentially verify that the
        //                 indices fit within the expected start/len of the journal buffer, and do
        //                 not collide with other entries.
        processor->ProcessWorkEntry(fbl::move(entry));
    }

    // Since the processor queues are accessed exclusively by the async thread,
    // we do not need to hold the lock while we access them.

    // If we processed any entries during the work step,
    // enqueue the dummy work to kick off the writeback queue.
    processor->EnqueueWork();

    // TODO(planders): Instead of immediately processing all wait items, wait until some
    //                 condition is fulfilled (e.g. journal is x% full, y total entries are
    //                 waiting, z time has passed, etc.) and write all entries out to disk at
    //                 once.
    // Process all entries in the "wait" queue. These are all transactions with entries that
    // have been enqueued to disk, and are waiting to verify that the write has completed.
    processor->ProcessWaitQueue();

    // TODO(planders): Similarly to the wait queue, instead of immediately processing all delete
    //                 items, wait until some condition is fulfilled and process all journal
    //                 deletions at once.

    // Track which entries have been fully persisted to their final on disk-location. Once we
    // have received verification that they have successfully completed, we can remove them
    // from the journal buffer to make space for new entries.
    processor->ProcessDeleteQueue();

    if (processor->HasError()) {
        {
            fbl::AutoLock lock(&lock_);

            // The thread signalling us should already be setting the Journal to read_only_, but in
            // case we managed to grab the lock first, set it again here.
            state_ = WritebackState::kReadOnly;

            // Reset the journal length to unblock transactions awaiting space,
            // No more writes to the buffer will be allowed.
            entries_->FreeAllSpace();
        }

        // Reset any pending delete requests (if any exist).
        processor->ResetWork();
    } else if (processor->GetBlocksProcessed() > 0) {
        uint64_t start, length;

        {
            fbl::AutoLock lock(&lock_);

            // Update the journal start/len to reflect the number of blocks that have been fully
            // processed.
            entries_->FreeSpace(processor->GetBlocksProcessed());

            start = entries_->start();
            length = entries_->length();
        }

        // The journal start/len have changed, so write out the info block.
        WriteInfo(start, length);

        // After the super block update has been queued for writeback, we can now "delete"
        // the entries that were previously pointed to by the info block. This must be done
        // after the info block write so that the info block does not point to invalid
        // entries.
        processor->EnqueueWork();
    }

    // If we are not in an error state and did not process any blocks, then the
    // JournalProcessor's work should be not have been initialized. This condition will be
    // checked at the beginning of the next call to ProcessQueue.

    // Since none of the methods in the kSync profile indicate that an entry should be added to
    // the next queue, it should be fine to pass a null output queue here.
    processor->ProcessSyncQueue();
}

void Journal::ProcessLoop() {
    {
        fbl::AutoLock lock(&lock_);
        ZX_DEBUG_ASSERT(state_ == WritebackState::kReady);
        state_ = WritebackState::kRunning;
    }

    JournalProcessor processor(this);
    while (true) {
        ProcessQueues(&processor);

        fbl::AutoLock lock(&lock_);

        // Signal the producer queue that space in the journal has (possibly) been freed up.
        cnd_signal(&producer_cvar_);

        // Before waiting, we should check if we're unmounting.
        if (unmounting_ && work_queue_.is_empty() && processor.IsEmpty() &&
            producer_queue_.is_empty()) {
            // Only return if we are unmounting AND all entries in all queues have been
            // processed. This includes producers which are currently waiting to be enqueued.
            break;
        }

        // If we received a signal while we were processing other queues,
        // immediately start processing again.
        if (!consumer_signalled_) {
            cnd_wait(&consumer_cvar_, lock_.GetInternal());
        }

        consumer_signalled_ = false;
    }
}

void JournalProcessor::ProcessWorkEntry(fbl::unique_ptr<JournalEntry> entry) {
    SetContext(ProcessorContext::kWork);
    ProcessResult result = ProcessEntry(entry.get());
    ZX_DEBUG_ASSERT(result == ProcessResult::kContinue);

    // Enqueue the entry into the wait_queue, even in the case of error. This is so that
    // all works contained by journal entries will be processed in the second step, even if
    // we do not plan to send them along to the writeback queue.
    wait_queue_.push(fbl::move(entry));
}

void JournalProcessor::ProcessWaitQueue() {
    SetContext(ProcessorContext::kWait);
    ProcessQueue(&wait_queue_, &delete_queue_);
}

void JournalProcessor::ProcessDeleteQueue() {
    SetContext(ProcessorContext::kDelete);
    ProcessQueue(&delete_queue_, &sync_queue_);
}
void JournalProcessor::ProcessSyncQueue() {
    SetContext(ProcessorContext::kSync);
    ProcessQueue(&sync_queue_, nullptr);
}

void JournalProcessor::SetContext(ProcessorContext context) {
    if (context_ != context) {
        // If we are switching from the sync profile, sync queue must be empty.
        ZX_DEBUG_ASSERT(context_ != ProcessorContext::kSync || sync_queue_.is_empty());

        switch (context) {
        case ProcessorContext::kDefault:
            ZX_DEBUG_ASSERT(context_ == ProcessorContext::kSync);
            break;
        case ProcessorContext::kWork:
            ZX_DEBUG_ASSERT(context_ == ProcessorContext::kDefault ||
                            context_ == ProcessorContext::kSync);
            break;
        case ProcessorContext::kWait:
            ZX_DEBUG_ASSERT(context_ != ProcessorContext::kDelete);
            break;
        case ProcessorContext::kDelete:
            ZX_DEBUG_ASSERT(context_ == ProcessorContext::kWait);
            break;
        case ProcessorContext::kSync:
            ZX_DEBUG_ASSERT(context_ == ProcessorContext::kDelete);
            break;
        default:
            ZX_DEBUG_ASSERT(false);
        }

        // Make sure that if a WritebackWork was established,
        // it was removed before we attempt to switch profiles.
        ZX_DEBUG_ASSERT(work_ == nullptr);
        blocks_processed_ = 0;
        context_ = context;
    }
}

void JournalProcessor::ProcessQueue(EntryQueue* in_queue, EntryQueue* out_queue) {
    // Process queue entries until there are none left, or we are told to wait.
    while (!in_queue->is_empty()) {
        // Process the entry before removing it from the queue.
        // If its status is kWaiting, we don't want to remove it.
        ProcessResult result = ProcessEntry(&in_queue->front());

        if (result == ProcessResult::kWait) {
            break;
        }

        auto entry = in_queue->pop();

        if (result == ProcessResult::kContinue) {
            ZX_DEBUG_ASSERT(out_queue != nullptr);
            out_queue->push(fbl::move(entry));
        } else {
            ZX_DEBUG_ASSERT(result == ProcessResult::kRemove);
        }
    }
}

ProcessResult JournalProcessor::ProcessEntry(JournalEntry* entry) {
    ZX_DEBUG_ASSERT(entry != nullptr);

    // Retrieve the entry status once up front so we don't have to keep atomically loading it.
    EntryStatus entry_status = entry->GetStatus();

    if (entry_status == EntryStatus::kWaiting) {
        // If the entry at the front of the queue is still waiting, we are done processing this
        // queue for the time being.
        return ProcessResult::kWait;
    }

    if (error_ && entry_status != EntryStatus::kSync) {
        // If we are in an error state and the entry is not a "sync" entry,
        // set the state to error so we do not do any unnecessary work.
        //
        // Since the error state takes precedence over the entry state,
        // we do not also have to set the entry state to error.
        entry_status = EntryStatus::kError;
    }

    if (entry_status == EntryStatus::kInit && context_ == ProcessorContext::kWork) {
        return ProcessWorkDefault(entry);
    }

    if (entry_status == EntryStatus::kPersisted) {
        if (context_ == ProcessorContext::kWait) {
            return ProcessWaitDefault(entry);
        }

        if (context_ == ProcessorContext::kDelete) {
            return ProcessDeleteDefault(entry);
        }
    }

    if (entry_status == EntryStatus::kSync) {
        if (context_ == ProcessorContext::kSync) {
            return ProcessSyncComplete(entry);
        }

        if (context_ != ProcessorContext::kDefault) {
            return ProcessSyncDefault(entry);
        }
    }

    if (entry_status == EntryStatus::kError) {
        if (context_ == ProcessorContext::kWork) {
            return ProcessErrorDefault();
        }

        if (context_ == ProcessorContext::kWait || context_ == ProcessorContext::kDelete) {
            return ProcessErrorComplete(entry);
        }
    }

    return ProcessUnsupported();
}

ProcessResult JournalProcessor::ProcessWorkDefault(JournalEntry* entry) {
    // If the entry is in the "init" state, we can now prepare its header/commit blocks
    // in the journal buffer.
    journal_->PrepareBuffer(entry);
    EntryStatus last_status = entry->SetStatus(EntryStatus::kWaiting);

    if (last_status == EntryStatus::kError) {
        // If the WritebackThread has failed and set our journal entry to an error
        // state in the time it's taken to prepare the buffer, set error state to
        // true. If we do not check this and continue having set the status to
        // kWaiting, we will never get another callback for this journal entry and
        // we will be stuck forever waiting for it to complete.
        error_ = true;
        entry->SetStatus(EntryStatus::kError);
    } else {
        ZX_DEBUG_ASSERT(last_status == EntryStatus::kInit);
        if (work_ == nullptr) {
            // Prepare a "dummy" work to kick off the writeback queue now that our entry is ready.
            // This is unnecessary in the case of an error, since the writeback queue will already
            // be failing all incoming transactions.
            work_ = journal_->CreateWork();
        }
    }

    return ProcessResult::kContinue;
}

ProcessResult JournalProcessor::ProcessWaitDefault(JournalEntry* entry) {
    EntryStatus last_status = entry->SetStatus(EntryStatus::kWaiting);
    ZX_DEBUG_ASSERT(last_status == EntryStatus::kPersisted);
    fbl::unique_ptr<WritebackWork> work = entry->TakeWork();
    journal_->EnqueueEntryWork(fbl::move(work));
    return ProcessResult::kContinue;
}

ProcessResult JournalProcessor::ProcessDeleteDefault(JournalEntry* entry) {
    if (work_ == nullptr) {
        // Use this work to enqueue any "delete" transactions we may encounter,
        // to be written after the info block is updated.
        work_ = journal_->CreateWork();
    }

    // The entry has now been fully persisted to disk, so we can remove the entry from
    // the journal. To ensure that it does not later get replayed unnecessarily, clear
    // out the header and commit blocks.
    journal_->PrepareDelete(entry, work_.get());

    // Track the number of blocks that have been fully processed so we can update the buffer.
    blocks_processed_ += entry->BlockCount();

    // We have fully processed this entry - do not add it to the next queue.
    return ProcessResult::kRemove;
}

ProcessResult JournalProcessor::ProcessSyncDefault(JournalEntry* entry) {
    // This is a sync request. Since there is no actual data to update,
    // we can just verify it and send it along to the next queue.
    ZX_DEBUG_ASSERT(entry->BlockCount() == 0);
    ZX_DEBUG_ASSERT(entry->GetHeaderIndex() == journal_->entries_->capacity());
    ZX_DEBUG_ASSERT(entry->GetCommitIndex() == journal_->entries_->capacity());

    // Always push the sync entry into the output queue.
    return ProcessResult::kContinue;
}

ProcessResult JournalProcessor::ProcessSyncComplete(JournalEntry* entry) {
    // Call the default sync method to ensure the entry matches what we expect.
    ProcessSyncDefault(entry);

    // Remove and enqueue the sync work.
    fbl::unique_ptr<WritebackWork> work = entry->TakeWork();
    journal_->EnqueueEntryWork(fbl::move(work));

    // The sync entry is complete; do not re-enqueue it.
    return ProcessResult::kRemove;
}

ProcessResult JournalProcessor::ProcessErrorDefault() {
    error_ = true;
    return ProcessResult::kContinue;
}

ProcessResult JournalProcessor::ProcessErrorComplete(JournalEntry* entry) {
    // If we are in an error state, force reset the entry's work. This will remove all
    // requests and call the sync closure (if it exists), thus completing this entry.
    entry->ForceReset();
    error_ = true;

    // Since all work is completed for this entry, we no longer need to send it along
    // to the next queue. Instead proceed to process the next entry.
    return ProcessResult::kRemove;
}

ProcessResult JournalProcessor::ProcessUnsupported() {
    ZX_ASSERT(false);
    return ProcessResult::kRemove;
}

} // blobfs
