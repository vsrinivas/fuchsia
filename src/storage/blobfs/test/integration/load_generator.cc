// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "load_generator.h"

using blobfs::BlobInfo;
using blobfs::GenerateRandomBlob;
using blobfs::StreamAll;
using blobfs::VerifyContents;

// Make sure we do not exceed maximum fd count.
static_assert(FDIO_MAX_FD >= 256, "");
constexpr uint32_t kMaxBlobs = FDIO_MAX_FD - 32;

enum class BlobList::QueueId : uint32_t {
  kCreated = 0,
  kTruncated,
  kWritten
};

void BlobList::GenerateLoad(uint32_t num_operations, unsigned int* rand_state) {
  for (uint32_t i = 0; i < num_operations; ++i) {
    switch (rand_r(rand_state) % 6) {
      case 0:
        CreateBlob(rand_state);
        break;
      case 1:
        TruncateBlob();
        break;
      case 2:
        WriteData();
        break;
      case 3:
        ReadData();
        break;
      case 4:
        ReopenBlob();
        break;
      case 5:
        UnlinkBlob(rand_state);
        break;
    }
    ASSERT_NO_FAILURES();
  }
}

void BlobList::VerifyFiles() {
  fbl::AutoLock al(&list_lock_);
  for (auto it = lists_[static_cast<uint32_t>(QueueId::kWritten)].begin();
       it != lists_[static_cast<uint32_t>(QueueId::kWritten)].end(); ++it) {
    it->fd.reset(open(it->info->path, O_RDONLY));
    ASSERT_NO_FAILURES(VerifyContents(it->fd.get(), it->info->data.get(), it->info->size_data));
  }
}

void BlobList::CloseFiles() {
  CloseFilesFromQueue(QueueId::kCreated);
  CloseFilesFromQueue(QueueId::kTruncated);
  CloseFilesFromQueue(QueueId::kWritten);
}

BlobFile BlobList::GetFileFrom(QueueId queue) {
  BlobFile file;
  fbl::AutoLock al(&list_lock_);
  if (lists_[static_cast<uint32_t>(queue)].empty()) {
    return file;
  }

  file = std::move(lists_[static_cast<uint32_t>(queue)].front());
  lists_[static_cast<uint32_t>(queue)].pop_front();
  return file;
}

void BlobList::PushFileInto(QueueId queue, BlobFile file) {
  fbl::AutoLock al(&list_lock_);
  lists_[static_cast<uint32_t>(queue)].push_back(std::move(file));
}

BlobList::QueueId BlobList::GetRandomQueue(unsigned int* rand_state) const {
  uint32_t value = rand_r(rand_state) % static_cast<uint32_t>(QueueId::kWritten);
  return static_cast<QueueId>(value);
}

void BlobList::CloseFilesFromQueue(QueueId queue) {
  fbl::AutoLock al(&list_lock_);
  for (auto it = lists_[static_cast<uint32_t>(queue)].begin();
       it != lists_[static_cast<uint32_t>(queue)].end(); ++it) {
    it->fd.reset();
  }
}

void BlobList::CreateBlob(unsigned int* rand_state, size_t num_writes) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(mount_path_, 1 + (rand_r(rand_state) % (1 << 16)), &info));

  BlobFile file(std::move(info), num_writes);

  file.fd.reset(open(file.info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(file.fd);

  {
    fbl::AutoLock al(&list_lock_);

    if (blob_count_ < kMaxBlobs) {
      lists_[static_cast<uint32_t>(QueueId::kCreated)].push_back(std::move(file));
      blob_count_++;
    }
  }

  if (file.info) {
    // Failed to insert.
    ASSERT_EQ(0, unlink(file.info->path));
  }
}

void BlobList::TruncateBlob() {
  BlobFile file = GetFileFrom(QueueId::kCreated);
  if (!file.info) {
    return;
  }

  // If we are going to run out of space on the underlying blobfs partition, the
  // ZX_ERR_NO_SPACE is going to come up here. if we run out of space, put the
  // kEmpty blob back onto the blob list.
  if (ftruncate(file.fd.get(), file.info->size_data) != 0) {
    ASSERT_EQ(errno, ENOSPC, "ftruncate returned an unrecoverable error");
  }

  PushFileInto(QueueId::kTruncated, std::move(file));
}

void BlobList::WriteData() {
  BlobFile file = GetFileFrom(QueueId::kTruncated);
  if (!file.info) {
    return;
  }

  size_t to_write = file.bytes_remaining / file.writes_remaining;
  size_t bytes_offset = file.info->size_data - file.bytes_remaining;
  ASSERT_EQ(to_write, write(file.fd.get(), file.info->data.get() + bytes_offset, to_write));

  file.writes_remaining--;
  file.bytes_remaining -= to_write;

  if (file.bytes_remaining == 0) {
    PushFileInto(QueueId::kWritten, std::move(file));
  } else {
    PushFileInto(QueueId::kTruncated, std::move(file));
  }
}

void BlobList::ReadData() {
  BlobFile file = GetFileFrom(QueueId::kWritten);
  if (!file.info) {
    return;
  }

  ASSERT_NO_FAILURES(VerifyContents(file.fd.get(), file.info->data.get(), file.info->size_data));

  PushFileInto(QueueId::kWritten, std::move(file));
}

void BlobList::UnlinkBlob(unsigned int* rand_state) {
  QueueId queue = GetRandomQueue(rand_state);
  BlobFile file = GetFileFrom(queue);
  if (!file.info) {
    return;
  }

  ASSERT_EQ(0, unlink(file.info->path));
  file.fd.reset();
  {
    fbl::AutoLock al(&list_lock_);
    blob_count_--;
  }
}

void BlobList::ReopenBlob() {
  BlobFile file = GetFileFrom(QueueId::kWritten);
  if (!file.info) {
    return;
  }

  file.fd.reset(open(file.info->path, O_RDONLY));
  ASSERT_TRUE(file.fd);

  PushFileInto(QueueId::kWritten, std::move(file));
}
