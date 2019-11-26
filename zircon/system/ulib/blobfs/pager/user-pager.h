// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/pager.h>

namespace blobfs {

// The size of a transfer buffer for reading from storage.
//
// The decision to use a single global transfer buffer is arbitrary; a pool of them could also be
// available in the future for more fine-grained access. Moreover, the blobfs pager uses a single
// thread at the moment, so a global buffer should be sufficient.
//
// 256 MB; but the size is arbitrary, since pages will become decommitted as they are moved to
// destination VMOS.
constexpr uint64_t kTransferBufferSize = 256 * (1 << 20);

// Abstract class that encapsulates a user pager, its associated thread and transfer buffer. The
// child class will need to define the interface required to populate the transfer buffer with
// blocks read from storage.
class UserPager {
 public:
  UserPager() = default;
  virtual ~UserPager() = default;

  // Returns the pager handle.
  const zx::pager& Pager() const { return pager_; }

  // Returns the pager dispatcher.
  async_dispatcher_t* Dispatcher() const { return pager_loop_.dispatcher(); }

  // Invoked by the |PageWatcher| on a read request. Reads in the requested block range
  // [|start_block|, |start_block| + |block_count|) for the inode associated with |map_index| into
  // the |transfer_buffer_|, and then moves those pages to the destination |vmo|.
  zx_status_t TransferPagesToVmo(uint32_t map_index, uint32_t start_block, uint32_t block_count,
                                 const zx::vmo& vmo);

 protected:
  // Sets up the transfer buffer, creates the pager and starts the pager thread.
  zx_status_t InitPager();

 private:
  // Virtual function to attach the transfer buffer to the underlying block device, so that blocks
  // can be read into it from storage.
  virtual zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) = 0;

  // Virtual function to read blocks corresponding to |map_index| in the range specified by
  // |start_block| and |block_count| into the transfer buffer.
  virtual zx_status_t PopulateTransferVmo(uint32_t map_index, uint32_t start_block,
                                          uint32_t block_count) = 0;

  // Scratch buffer for pager transfers.
  // NOTE: Per the constraints imposed by |zx_pager_supply_pages|, this can never be mapped.
  zx::vmo transfer_buffer_;

  // Async loop for pager requests.
  async::Loop pager_loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::pager pager_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_
