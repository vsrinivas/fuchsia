// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fcntl.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-test-utils/blobfs/blobfs.h>
#include <fs-test-utils/fixture.h>
#include <lib/fdio/io.h>
#include <zircon/thread_annotations.h>
#include <unittest/unittest.h>

namespace fs_test_utils {

// Make sure we do not exceed maximum fd count
static_assert(FDIO_MAX_FD >= 256, "");
constexpr uint32_t kMaxBlobs = FDIO_MAX_FD - 32;

enum class TestState {
    kEmpty,
    kConfigured,
    kReadable,
};

struct BlobState : public fbl::DoublyLinkedListable<fbl::unique_ptr<BlobState>> {
    BlobState(fbl::unique_ptr<BlobInfo> i, size_t writes_remaining)
    : info(std::move(i)), state(TestState::kEmpty), writes_remaining(writes_remaining) {
        bytes_remaining = info->size_data;
    }

    fbl::unique_ptr<BlobInfo> info;
    TestState state;
    fbl::unique_fd fd;
    size_t writes_remaining;
    size_t bytes_remaining;
};

enum class BlobListState {
    kOpen,
    kClosed,
};

// Provides a structure for keeping track of and manipulating blobs for tests.
// The CreateBlob, ConfigBlob, WriteData, ReadData, UnlinkBlob, and ReopenBlob
// functions all operate on one blob at a time. The specific blob they operate
// on is an implementation detail of this class. It's intended that this is used
// for doing large scale tests with lots of operations where the specific
// operations are less important, and it's just important that things are
// happening.
class BlobList {
public:
    // Create a new blob list, storing the blobs on an existing blobfs partition
    // that is mounted at mount_point.
    explicit BlobList(fbl::String mount_path) : mount_path_(mount_path) {}

    // Create a new blob entry in the blob list. A file descriptor is opened for
    // this blob, but no data is written. The seed is stored in the random blob
    // generator for future use (see GenerateRandomBlob).
    //
    // The blob can be written to once before it is considered read-only.
    //
    // This function is thread-safe.
    bool CreateBlob(unsigned* seed);

    // Create a new blob entry in the blob list. A file descriptor is opened for
    // this blob, but no data is written. The seed is stored in the random blob
    // generator for future use (see GenerateRandomBlob).
    //
    // writes_remaining describes how many times the blob will be written to
    // before it's considered fully written and transitions from a write-only to
    // a read-only state.
    //
    // This function is thread-safe.
    bool CreateBlob(unsigned* seed, size_t writes_remaining);

    // Truncate the blob on disk to the size of the randomly generated data.
    // This is a no-op if the blob wasn't freshly created.
    //
    // This function is thread-safe.
    bool ConfigBlob();

    // Perform a write of random data to the blob. If this exausts the number of
    // writes remaining (default 1), it sets the blob read-only. It's a no-op if
    // the blob hasn't been configured or is already read-only.
    //
    // This function is thread-safe.
    bool WriteData();

    // Verify that the contents of the blob are both readable and valid. This is
    // a no-op if the data is not finished being written.
    //
    // This function is thread-safe.
    bool ReadData();

    // Remove the blob from the blob list and unlinks the blob from the
    // underlying filesystem.
    //
    // This function is thread-safe.
    bool UnlinkBlob();

    // Close the blob and re-open it. This is a no-op if the data is not
    // finished being written.
    //
    // This function is thread-safe.
    bool ReopenBlob();

    // Verify the contents of all fully-written blobs in the blob list. Blobs
    // that are not yet fully written are ignored.
    //
    // This function is thread-safe.
    bool VerifyAll();

    // A note on the thread-safety of the following functions - CloseAll and
    // OpenAll change the blob list state and manipulate the underlying file
    // descriptors of all of the blobs. The only way to make them able to be run
    // concurrently with the other methods on the blob list is for all functions
    // to hold the blob list lock for their whole execution, which would greatly
    // inhibit concurrent functionality. Therefore, to use these functions, the
    // caller must guarantee that no other functions are concurrently invoked.

    // Close the file descriptors for all the blobs in the blob list, REGARDLESS
    // of state. Blobs that were not fully written at this time will be removed
    // from the bloblist.
    //
    // The blob list will be set to a closed state - this implies that all the
    // underlying file descriptors for the blobs are closed. The only valid
    // operation on a closed blob list is OpenAll.
    //
    // This function is NOT thread-safe.
    bool CloseAll();

    // Open the file desciptors for all the blobs in the blob list. This
    // operation is only valid if the list has been previously closed.
    //
    // This function is NOT thread-safe.
    bool OpenAll();

private:
    fbl::String mount_path_;
    fbl::Mutex list_lock_;
    fbl::DoublyLinkedList<fbl::unique_ptr<BlobState>> list_ TA_GUARDED(list_lock_);
    uint32_t blob_count_ TA_GUARDED(list_lock_) = 0;
    BlobListState list_state_ = BlobListState::kOpen;
};

} // namespace fs_test_utils
