// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_DEVICE_MANAGER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_DEVICE_MANAGER_H_

#include <fidl/fuchsia.hardware.block.encrypted/cpp/wire.h>
#include <lib/ddk/device.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ddktl/device.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

#include "lib/inspect/cpp/inspector.h"
#include "src/security/zxcrypt/volume.h"

namespace zxcrypt {

class Device;

// |zxcrypt::DeviceManager| is a "wrapper" driver for zxcrypt volumes.  Each block device with valid
// zxcrypt metadata will result in a wrapper being created, but the wrapper cannot perform any block
// operations.  To perform block operations, |Unseal| must first be called with a valid key and
// slot, which will cause an unsealed |zxcrypt::Device| to be added to the device tree.
class DeviceManager;
using DeviceManagerType =
    ddk::Device<DeviceManager, ddk::Unbindable,
                ddk::Messageable<fuchsia_hardware_block_encrypted::DeviceManager>::Mixin,
                ddk::ChildPreReleaseable>;
class DeviceManager final : public DeviceManagerType {
 public:
  explicit DeviceManager(zx_device_t* parent) : DeviceManagerType(parent), state_(kBinding) {}
  ~DeviceManager() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(DeviceManager);

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Adds the device
  zx_status_t Bind();

  // ddk::Device methods; see ddktl/device.h
  void DdkUnbind(ddk::UnbindTxn txn) __TA_EXCLUDES(mtx_);
  void DdkRelease();

  // ddk::ChildPreRelease methods
  void DdkChildPreRelease(void* child_ctx) __TA_EXCLUDES(mtx_);

  // Formats the zxcrypt volume, destroying any data contained therein, and
  // enrolls the given key in the requested key slot.  Leaves the device sealed.
  void Format(FormatRequestView request, FormatCompleter::Sync& completer) __TA_EXCLUDES(mtx_);

  // Unseals the zxcrypt volume and adds it as a |zxcrypt::Device| to the device tree.
  void Unseal(UnsealRequestView request, UnsealCompleter::Sync& completer) __TA_EXCLUDES(mtx_);

  // Removes the unsealed |zxcrypt::Device|, if present.
  void Seal(SealCompleter::Sync& completer) __TA_EXCLUDES(mtx_);

  // Clobbers the superblock (and any backup superblocks), preventing future
  // Unseal operations from succeeding (provided no other program is
  // manipulating the underlying block device).
  void Shred(ShredCompleter::Sync& completer) __TA_EXCLUDES(mtx_);

 private:
  // Represents the state of this device.  State transitions:
  //
  // kBinding is initial state
  // kBinding -> kSealed on DdkAdd success
  // kBinding -> kRemoved on DdkAdd failure
  //
  // kSealed -> kUnsealed if Unseal called with correct key
  // kSealed -> kRemoved on DdkUnbind
  //
  // kUnsealed -> kSealing if Seal called
  // kUnsealed -> kUnsealedShredded if Shred called
  // kUnsealed -> kRemoved on DdkUnbind
  //
  // kSealing -> kSealed when DdkChildPreRelease hook called
  // kSealing -> kRemoved on DdkUnbind
  //
  // kUnsealedShredded -> kSealing if Seal called
  // kUnsealedShredded -> kRemoved on DdkUnbind
  //
  // kRemoved is terminal
  enum State {
    kBinding,
    kSealed,
    kUnsealed,
    kUnsealedShredded,
    kSealing,
    kRemoved,
  };

  // Formats the zxcrypt volume, enrolling |ikm| in key slot |slot|.  Keeps the
  // device sealed.
  zx_status_t FormatLocked(const uint8_t* ikm, size_t ikm_len, key_slot_t slot) __TA_REQUIRES(mtx_);

  // Unseals the zxcrypt volume and adds it as a |zxcrypt::Device| to the device tree.
  zx_status_t UnsealLocked(const uint8_t* ikm, size_t ikm_len, key_slot_t slot) __TA_REQUIRES(mtx_);

  // Transition to the specified |state|.
  void TransitionState(State state, std::optional<zxcrypt::Device*> child,
                       std::optional<SealCompleter::Async> seal_completer) __TA_REQUIRES(mtx_);
  void TransitionState(State state, std::optional<zxcrypt::Device*> child) __TA_REQUIRES(mtx_);
  void TransitionState(State state) __TA_REQUIRES(mtx_);

  // Used to ensure calls to |Unseal|, |Seal|, and |Unbind| are exclusive to each
  // other, and protects access to |state_|.
  fbl::Mutex mtx_;

  // Used for debug state.
  inspect::Inspector inspect_;

  // Child `unsealed` device, if present.  Owned by device_manager, but it'll send us a
  // ChildPreRelease hook notification before it destroys it.
  std::optional<zxcrypt::Device*> child_ __TA_GUARDED(mtx_);

  // A place to hold a FIDL transaction completer so we can asynchronously
  // complete the transaction when device_manager confirms the removal of the
  // child device.
  std::optional<SealCompleter::Async> seal_completer_ __TA_GUARDED(mtx_);

  State state_ __TA_GUARDED(mtx_);
};

}  // namespace zxcrypt

#endif  // SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_DEVICE_MANAGER_H_
