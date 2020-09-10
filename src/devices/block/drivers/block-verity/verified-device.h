// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_VERIFIED_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_VERIFIED_DEVICE_H_

#include <lib/zx/vmo.h>
#include <zircon/device/block.h>
#include <zircon/listnode.h>

#include <memory>
#include <optional>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/mutex.h>

#include "src/devices/block/drivers/block-verity/block-loader-interface.h"
#include "src/devices/block/drivers/block-verity/block-verifier.h"
#include "src/devices/block/drivers/block-verity/device-info.h"

namespace block_verity {

// See ddk::Device in ddktl/device.h
class VerifiedDevice;
using VerifiedDeviceType =
    ddk::Device<VerifiedDevice, ddk::GetProtocolable, ddk::GetSizable, ddk::Unbindable>;

// A DDK device that speaks the ddk block protocol, providing a block device
// that exposes the data section of the device for reads.  It verifies each read
// against the integrity data merkle tree rooted in the hash provided at
// construction time.
class VerifiedDevice final : public VerifiedDeviceType,
                             public ddk::BlockImplProtocol<VerifiedDevice, ddk::base_protocol>,
                             public BlockLoaderInterface {
 public:
  VerifiedDevice(zx_device_t* parent, DeviceInfo&& info,
                 const std::array<uint8_t, kHashOutputSize>& integrity_root_hash);

  // Disallow copy, assign, and move.
  VerifiedDevice(const VerifiedDevice&) = delete;
  VerifiedDevice(VerifiedDevice&&) = delete;
  VerifiedDevice& operator=(const VerifiedDevice&) = delete;
  VerifiedDevice& operator=(VerifiedDevice&&) = delete;

  ~VerifiedDevice() = default;

  uint64_t op_size() { return info_.op_size; }

  // Do fallible construction and request BlockVerifier prepare for verified
  // reads.
  zx_status_t Init();

  // ddk::Device methods; see ddktl/device.h
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  zx_off_t DdkGetSize();
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // ddk::BlockProtocol methods; see ddktl/protocol/block.h
  void BlockImplQuery(block_info_t* out_info, size_t* out_op_size);
  void BlockImplQueue(block_op_t* block_op, block_impl_queue_callback completion_cb, void* cookie)
      __TA_EXCLUDES(mtx_);

  // `block_verity::BlockLoaderInterface`
  void RequestBlocks(uint64_t start_block, uint64_t block_count, zx::vmo& vmo, void* cookie,
                     BlockLoaderCallback callback) override;

  // Callback for reads initiated by `RequestBlocks` (the `BlockLoaderInterface`
  // implementation)
  void OnBlockLoaderRequestComplete(zx_status_t status, block_op_t* block);

  // The callback that we give to the underlying block device when we queue
  // operations against it.  It simply translates block offsets back and completes the
  // matched block requests.
  void OnClientBlockRequestComplete(zx_status_t status, block_op_t* block);

  // Callback for `BlockVerifier::PrepareAsync`
  void OnBlockVerifierPrepareComplete(zx_status_t status);

  // Completes the block operation by calling the appropriate callback with the
  // appropriate status.
  void BlockComplete(block_op_t* block, zx_status_t status) __TA_REQUIRES(mtx_);

 private:
  void ForwardTranslatedBlockOp(block_op_t* block_op) __TA_REQUIRES(mtx_);

  // Completes the UnbindTxn if outstanding_block_requests_ has gone to 0.
  void TeardownIfQuiesced() __TA_REQUIRES(mtx_);

  enum DeviceState {
    // The device is not ready.  It will transition to kLoading when
    // Init is called.
    kInitial,

    // The device is waiting for integrity data to be read in from disk.
    kLoading,

    // The device is ready to serve read requests.
    kActive,

    // The device has been told to unbind and is completing queued requests, but
    // rejects new requests.
    kQuiescing,

    // The device has completed teardown and is ready to be removed.
    kStopped,

    // The device has hit an unrecoverable error and will fail all requests
    // until unbound.
    kFailed,
  };

  // Current device state.
  DeviceState state_ __TA_GUARDED(mtx_);

  fbl::Mutex mtx_;

  // A single block op request buffer, allocated to be the size of the parent
  // block op size request.
  std::unique_ptr<uint8_t[]> block_op_buf_;

  // Tracks the number of block I/O requests that we have sent to the backing
  // storage but that have not yet completed.  We need to wait for this to go to
  // zero before we complete unbinding.
  uint64_t outstanding_block_requests_ __TA_GUARDED(mtx_);

  // A linked list of block requests that we have received while state was `kLoading`
  // which we have deferred passing to the block driver until after integrity
  // data is loaded.  Should be empty unless state is `kLoading` and we've
  // received inbound block requests; after transitioning to `kActive`, this
  // should be an empty list.
  list_node_t deferred_requests_ __TA_GUARDED(mtx_);

  // Device configuration, as provided by the DeviceManager at creation. Its
  // constness allows it to be used without holding the lock.
  const DeviceInfo info_;

  // A reference to an unbind transaction when we need to delay replying until
  // we've completed some other work.
  std::optional<ddk::UnbindTxn> unbind_txn_;

  // Verifies data blocks that we've loaded against integrity information.  Used
  // to ensure we complete reads successfully iff the block data matches the
  // integrity data.
  BlockVerifier block_verifier_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_VERIFIED_DEVICE_H_
