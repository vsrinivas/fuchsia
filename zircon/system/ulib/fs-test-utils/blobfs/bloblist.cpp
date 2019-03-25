// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-test-utils/blobfs/blobfs.h>
#include <fs-test-utils/blobfs/bloblist.h>
#include <lib/fdio/io.h>
#include <unittest/unittest.h>

namespace fs_test_utils {

bool BlobList::CreateBlob(unsigned* seed) {
    return CreateBlob(seed, 1);
}

// Generate and open a new blob
bool BlobList::CreateBlob(unsigned* seed, size_t writes_remaining) {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kOpen);

    fbl::unique_ptr<BlobInfo> info;
    ASSERT_TRUE(GenerateRandomBlob(mount_path_, 1 + (rand_r(seed) % (1 << 16)), &info));

    fbl::AllocChecker ac;
    fbl::unique_ptr<BlobState> state(new (&ac) BlobState(std::move(info), writes_remaining));
    ASSERT_EQ(ac.check(), true);

    {
        fbl::AutoLock al(&list_lock_);

        if (blob_count_ >= kMaxBlobs) {
            return true;
        }
        fbl::unique_fd fd(open(state->info->path, O_CREAT | O_RDWR));
        ASSERT_TRUE(fd, "Failed to create blob");
        state->fd.reset(fd.release());

        list_.push_front(std::move(state));
        blob_count_++;
    }
    END_HELPER;
}

// Allocate space for an open, empty blob
bool BlobList::ConfigBlob() {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kOpen);

    fbl::unique_ptr<BlobState> state;
    {
        fbl::AutoLock al(&list_lock_);
        state = list_.pop_back();
    }

    if (state == nullptr) {
        return true;
    } else if (state->state == TestState::kEmpty) {
        ASSERT_EQ(ftruncate(state->fd.get(), state->info->size_data), 0);
        state->state = TestState::kConfigured;
    }
    {
        fbl::AutoLock al(&list_lock_);
        list_.push_front(std::move(state));
    }
    END_HELPER;
}

// Write the data for an open, partially written blob
bool BlobList::WriteData() {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kOpen);

    fbl::unique_ptr<BlobState> state;
    {
        fbl::AutoLock al(&list_lock_);
        state = list_.pop_back();
    }
    if (state == nullptr) {
        return true;
    } else if (state->state == TestState::kConfigured) {
        size_t bytes_write = state->bytes_remaining / state->writes_remaining;
        size_t bytes_offset = state->info->size_data - state->bytes_remaining;
        ASSERT_EQ(StreamAll(write, state->fd.get(), state->info->data.get() + bytes_offset,
                            bytes_write), 0, "Failed to write Data");

        state->writes_remaining--;
        state->bytes_remaining -= bytes_write;
        if (state->writes_remaining == 0 && state->bytes_remaining == 0) {
            state->state = TestState::kReadable;
        }
    }
    {
        fbl::AutoLock al(&list_lock_);
        list_.push_front(std::move(state));
    }
    END_HELPER;
}

// Read the blob's data
bool BlobList::ReadData() {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kOpen);

    fbl::unique_ptr<BlobState> state;
    {
        fbl::AutoLock al(&list_lock_);
        state = list_.pop_back();
    }
    if (state == nullptr) {
        return true;
    } else if (state->state == TestState::kReadable) {
        ASSERT_TRUE(VerifyContents(state->fd.get(), state->info->data.get(),
                                   state->info->size_data));
    }
    {
        fbl::AutoLock al(&list_lock_);
        list_.push_front(std::move(state));
    }
    END_HELPER;
}

// Unlink the blob
bool BlobList::UnlinkBlob() {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kOpen);

    fbl::unique_ptr<BlobState> state;
    {
        fbl::AutoLock al(&list_lock_);
        state = list_.pop_back();
    }
    if (state == nullptr) {
        return true;
    }
    ASSERT_EQ(unlink(state->info->path), 0, "Could not unlink blob");
    ASSERT_EQ(close(state->fd.release()), 0, "Could not close blob");
    {
        fbl::AutoLock al(&list_lock_);
        blob_count_--;
    }
    END_HELPER;
}

bool BlobList::ReopenBlob() {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kOpen);

    fbl::unique_ptr<BlobState> state;
    {
        fbl::AutoLock al(&list_lock_);
        state = list_.pop_back();
    }
    if (state == nullptr) {
        return true;
    } else if (state->state == TestState::kReadable) {
        ASSERT_EQ(close(state->fd.release()), 0, "Could not close blob");
        fbl::unique_fd fd(open(state->info->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to reopen blob");
        state->fd.reset(fd.release());
    }
    {
        fbl::AutoLock al(&list_lock_);
        list_.push_front(std::move(state));
    }
    END_HELPER;
}

bool BlobList::VerifyAll() {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kOpen);

    fbl::AutoLock al(&list_lock_);

    for (auto& state : list_) {
        if (state.state == TestState::kReadable) {
            ASSERT_TRUE(VerifyContents(state.fd.get(), state.info->data.get(),
                                       state.info->size_data));
        }
    }

    END_HELPER;
}

bool BlobList::CloseAll() {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kOpen);

    // the functions that act on all the blobs in the list are not really
    // thread-safe, but we are going to be good citizens anyway.
    fbl::AutoLock al(&list_lock_);

    fbl::DoublyLinkedList<fbl::unique_ptr<BlobState>> readable_list;
    fbl::unique_ptr<BlobState> state;
    while(!list_.is_empty()) {
        state = list_.pop_back();
        ASSERT_EQ(close(state->fd.release()), 0, "Could not close blob");
        // only put the blob back in the blob list if it's fully written.
        if (state->state == TestState::kReadable) {
            readable_list.push_front(std::move(state));
        }
    }

    list_ = std::move(readable_list);
    list_state_ = BlobListState::kClosed;

    END_HELPER;
}

bool BlobList::OpenAll() {
    BEGIN_HELPER;
    ASSERT_EQ(list_state_, BlobListState::kClosed);

    // the functions that act on all the blobs in the list are not really
    // thread-safe, but we are going to be good citizens anyway.
    fbl::AutoLock al(&list_lock_);

    for (auto& state : list_) {
        if (state.state == TestState::kReadable) {
            fbl::unique_fd fd(open(state.info->path, O_RDONLY));
            ASSERT_TRUE(fd, "Failed to open blob");
            state.fd.reset(fd.release());
        } else { // kEmpty, kConfig
            // if a blob was not fully written by the time it was closed, it
            // should be gone.
            ASSERT_LT(open(state.info->path, O_RDONLY), 0);
        }
    }

    list_state_ = BlobListState::kOpen;

    END_HELPER;
}

} // namespace fs_test_utils
