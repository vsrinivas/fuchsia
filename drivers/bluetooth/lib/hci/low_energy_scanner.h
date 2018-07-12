// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>

#include <lib/async/dispatcher.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace btlib {
namespace hci {

class Transport;

// Represents a discovered Bluetooth Low Energy device.
struct LowEnergyScanResult {
  LowEnergyScanResult();
  LowEnergyScanResult(const common::DeviceAddress& address,
                      bool connectable,
                      int8_t rssi);

  // The device address of the remote device.
  // TODO(armansito): Report resolved address if address is resolvable and we
  // can resolve it.
  common::DeviceAddress address;

  // True if this device accepts connections. This is the case if this device
  // sent a connectable advertising PDU.
  bool connectable;

  // The received signal strength of the advertisement packet corresponding to
  // this device.
  int8_t rssi;
};

// LowEnergyScanner manages Low Energy device scan procedures that are used
// during general and limited device discovery and connection establishment
// procedures. This is an abstract class that provides a common interface
// over 5.0 Extended Advertising and Legacy Advertising features.
//
// Instances of this class are expected to each as a singleton on a
// per-transport basis as multiple instances cannot accurately reflect the state
// of the controller while allowing simultaneous scan operations.
class LowEnergyScanner {
 public:
  // Value that can be passed to StartScan() to scan indefinitely.
  static constexpr int64_t kPeriodInfinite = -1;

  enum class State {
    // No scan is currently being performed.
    kIdle,

    // A previously running scan is being stopped.
    kStopping,

    // A scan is being initiated.
    kInitiating,

    // A scan is currently being performed.
    kScanning,
  };

  // Interface for receiving events related to Low Energy device scan.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when a device is found. |data| contains the advertising data, as
    // well as any scan response data that was received during an active scan.
    virtual void OnDeviceFound(const LowEnergyScanResult& result,
                               const common::ByteBuffer& data);

    // TODO(armansito): Add function for directed advertising reports.
  };

  LowEnergyScanner(Delegate* delegate,
                   fxl::RefPtr<Transport> hci,
                   async_dispatcher_t* dispatcher);
  virtual ~LowEnergyScanner() = default;

  // Returns the current Scan state.
  State state() const { return state_; }

  // True if a device scan is currently being performed.
  bool IsScanning() const { return state() == State::kScanning; }

  // Initiates a device scan. This is an asynchronous operation that abides by
  // the following rules:
  //
  //   - This method synchronously returns false if the procedure could not be
  //     started, e.g. because discovery is already in progress, or it is in the
  //     process of being stopped, or the controller does not support discovery,
  //     etc.
  //
  //   - Synchronously returns true if the procedure was initiated but the it is
  //     unknown whether or not the procedure has succeeded.
  //
  //   - |callback| is invoked asynchronously to report the status of the
  //     procedure. In the case of failure, |callback| will be invoked once to
  //     report the end of the procedure. In the case of success, |callback|
  //     will be invoked twice: the first time to report that the procedure has
  //     started, and a second time to report when the procedure ends, either
  //     due to a timeout or cancellation.
  //
  //   - |period| specifies (in milliseconds) the duration of the scan. If the
  //     special value of kPeriodInfinite is passed then scanning will continue
  //     indefinitely and must be explicitly stopped by calling StopScan().
  //     Otherwise, the value must be non-zero.
  //
  // Once started, a scan can be terminated at any time by calling the
  // StopScan() method. Otherwise, an ongoing scan will terminate at the end of
  // the scan period if a finite value for |period_ms| was provided.
  //
  // If an active scan is being performed, then scannable advertising reports
  // will NOT generate an OnDeviceFound event until a scan response is received
  // from the corresponding broadcaster. If a scan response from a scannable
  // device is never received during a scan period, then an OnDeviceFound event
  // (exluding scan response data) will be generated for that device at the end
  // of the scan period, UNLESS the scan was explicitly stopped via StopScan().
  enum class ScanStatus {
    // Reported when the scan could not be started.
    kFailed,

    // Reported when the scan was started and is currently in progress.
    kStarted,

    // Called when the scan was terminated naturally at the end of the scan
    // period.
    kComplete,

    // Called when the scan was terminated due to a call to StopScan().
    kStopped,
  };
  using ScanStatusCallback = fit::function<void(ScanStatus)>;
  virtual bool StartScan(bool active,
                         uint16_t scan_interval,
                         uint16_t scan_window,
                         bool filter_duplicates,
                         LEScanFilterPolicy filter_policy,
                         int64_t period_ms,
                         ScanStatusCallback callback) = 0;

  // Stops a previously started device scan. Returns false if a scan is not in
  // progress. Otherwise, cancels any in progress scan procedure and returns
  // true.
  virtual bool StopScan() = 0;

 protected:
  async_dispatcher_t* dispatcher() const { return dispatcher_; }
  Transport* transport() const { return transport_.get(); }
  SequentialCommandRunner* hci_cmd_runner() const {
    return hci_cmd_runner_.get();
  }
  Delegate* delegate() const { return delegate_; }

  void set_state(State state) { state_ = state; }

 private:
  State state_;

  Delegate* delegate_;  // weak

  // Task runner for all asynchronous tasks.
  async_dispatcher_t* dispatcher_;

  // The HCI transport.
  fxl::RefPtr<Transport> transport_;

  // Command runner for all HCI commands sent out by implementations.
  std::unique_ptr<SequentialCommandRunner> hci_cmd_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyScanner);
};

}  // namespace hci
}  // namespace btlib
