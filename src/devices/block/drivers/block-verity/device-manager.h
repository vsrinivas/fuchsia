// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_MANAGER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_MANAGER_H_

#include <fuchsia/hardware/block/verified/llcpp/fidl.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

#include "driver-sealer.h"

namespace block_verity {

class Device;

class DeviceManager;
using DeviceManagerType =
    ddk::Device<DeviceManager, ddk::UnbindableNew, ddk::Messageable, ddk::ChildPreReleaseable>;

// A device that consumes a block device and implements
// `fuchsia.hardware.block.verified`.  It manages the lifecycle of a child block
// device which represents either a mutable or verified view of another block
// device.
class DeviceManager final
    : public DeviceManagerType,
      public ::llcpp::fuchsia::hardware::block::verified::DeviceManager::Interface {
 public:
  explicit DeviceManager(zx_device_t* parent)
      : DeviceManagerType(parent),
        child_(std::nullopt),
        close_completer_(std::nullopt),
        state_(kBinding) {}
  // Disallow copy, assign, and move.
  DeviceManager(const DeviceManager&) = delete;
  DeviceManager(DeviceManager&&) = delete;
  DeviceManager& operator=(const DeviceManager&) = delete;
  DeviceManager& operator=(DeviceManager&&) = delete;

  ~DeviceManager() = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Adds the device
  zx_status_t Bind();

  // ddk::Device methods; see ddktl/device.h
  void DdkUnbindNew(ddk::UnbindTxn txn) __TA_EXCLUDES(mtx_);
  void DdkRelease();

  // ddk::Messageable methods
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) __TA_EXCLUDES(mtx_);

  // ddk::ChildPreRelease methods
  void DdkChildPreRelease(void* child_ctx);

  // implement `DeviceManager::Interface`
  void OpenForWrite(::llcpp::fuchsia::hardware::block::verified::Config config,
                    OpenForWriteCompleter::Sync completer) override __TA_EXCLUDES(mtx_);
  void CloseAndGenerateSeal(CloseAndGenerateSealCompleter::Sync completer) override
      __TA_EXCLUDES(mtx_);
  void OpenForVerifiedRead(::llcpp::fuchsia::hardware::block::verified::Config config,
                           ::llcpp::fuchsia::hardware::block::verified::Seal seal,
                           OpenForVerifiedReadCompleter::Sync completer) override
      __TA_EXCLUDES(mtx_);
  void Close(CloseCompleter::Sync completer) override __TA_EXCLUDES(mtx_);

  static void SealCompletedCallback(void* cookie, zx_status_t status, const uint8_t* seal_buf,
                                    size_t seal_len);
  void OnSealCompleted(zx_status_t status, const uint8_t* seal_buf, size_t seal_len);

 private:
  // Represents the state of this device.
  enum State {
    // Initial state upon allocation.  Transitions to `kClosed` during `Bind()`.
    kBinding,

    // No child devices exist.  Can transition to `kAuthoring` or `kVerifiedRead`
    // in response to FIDL request to open.
    kClosed,

    // The `mutable` child device is present and available for read/write
    // access.  This state can transition to `kClosing` via a `Close()` call  or
    // to `kClosingForSeal` via a `CloseAndGenerateSeal()` call.
    kAuthoring,

    // We have requested that the child device be closed, but it hasn't been
    // torn down yet.  When it does (which we'll be notified of via
    // `DdkChildPreRelease`), we'll transition to `kSealing`.
    kClosingForSeal,

    // The child device has been torn down.  We are recomputing all integrity
    // information and writing it out to the underlying block device, then we
    // will transition to `kClosed` and return the seal to the
    // `CloseAndGenerateSeal()` caller.
    kSealing,

    // The `verified` child device is present and available for readonly,
    // verified access.  This state can transition to `kClosing` via a `Close()`
    // call.
    kVerifiedRead,

    // Either the `mutable` or `verified` child device is present.  We have
    // requested that it be unbound, but have not yet heard via
    // `DdkChildPreRelease` that it has been unbound.  When we do, we will
    // transition to the `kClosed` state.
    kClosing,

    // Some underlying failure has left this device in an inconsistent state and
    // we refuse to go on.
    kError,

    // TODO: still working out the exact lifecycle implications of these states
    kUnbinding,
    kRemoved,
  };

  // If we are currently exposing a child device, this will be a reference to
  // the child so we can request it be removed.  This is expected to be
  // nonnull when `state_` is kAuthoring or kClosing.
  std::optional<block_verity::Device*> child_;

  // A place to hold a FIDL transaction completer so we can asynchronously
  // complete the transaction when we see the child device disappear, via the
  // `ChildPreRelease` hook.  This is expected to be valid when `state_` is
  // `kClosing` and nullopt all other times.
  std::optional<CloseCompleter::Async> close_completer_;

  // If we are currently sealing, this holds the DriverSealer which is responsible for
  // scheduling and performing that computation, then calling a callback.  This
  // is expected to be valid when `state_` is `kSealing` and nullopt all other
  // times.
  std::unique_ptr<DriverSealer> sealer_;

  // A place to hold a FIDL transaction completer so we can asynchronously
  // complete the transaction after doing a bunch of I/O to regenerate the
  // integrity data, superblock, and seal.  This is expected to be valid when
  // `state_` is `kClosingForSeal` and `kSealing`, and nullopt all other times.
  std::optional<CloseAndGenerateSealCompleter::Async> seal_completer_;

  // Used to ensure FIDL calls are exclusive to each other, and protects access to `state_`.
  fbl::Mutex mtx_;

  // What state is this device in?  See more details for the state machine above
  // where `State` is defined.
  State state_ __TA_GUARDED(mtx_);
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_MANAGER_H_
