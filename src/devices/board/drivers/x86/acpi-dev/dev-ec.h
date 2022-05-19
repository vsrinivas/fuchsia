// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/inspector.h>
#include <lib/zx/event.h>
#include <zircon/types.h>

#include <condition_variable>

#include <ddktl/device.h>

#include "src/devices/board/lib/acpi/acpi.h"

namespace acpi_ec {

// Commands.
enum EcCmd {
  kRead = 0x80,
  kWrite = 0x81,
  kQuery = 0x84,
};

// Status register bits.
enum EcStatus {
  kSciEvt = 1 << 5,
  kIbf = 1 << 1,
  kObf = 1 << 0,
};

enum EcSignal {
  // Status.IBF == 0, host can write next byte to EC.
  kCanWrite = ZX_USER_SIGNAL_0,
  // Status.OBF == 1, host can read byte from EC.
  kCanRead = ZX_USER_SIGNAL_1,
  // Status.SCI_EVT == 1, EC wants host to handle an event.
  kPendingEvent = ZX_USER_SIGNAL_2,
  // Driver is shutting down.
  kEcShutdown = ZX_USER_SIGNAL_3,
  // TXQ has items ready to be processed.
  kTransactionReady = ZX_USER_SIGNAL_4,
};

// Represents a single transaction going to or from the EC.
struct Transaction {
  // Operation to perform.
  EcCmd op;
  // For read or write, address to read/write. Ignored for query.
  uint8_t addr = 0;
  // For read: value that was read.
  // For write: value to write.
  // For query: event.
  uint8_t value = 0;
  // Status of the transaction.
  zx_status_t status = ZX_OK;
  // Signalled when this transaction is ready to be consumed by whatever initiated it.
  // Transactions are usually performed in synchronous contexts (i.e. AML code), so this is OK.
  sync_completion_t done;
};

class IoPortInterface {
 public:
  virtual uint8_t inp(uint16_t port) = 0;
  virtual void outp(uint16_t port, uint8_t value) = 0;

  virtual zx_status_t Map(uint16_t port) = 0;

  virtual ~IoPortInterface() = default;
};

// The interface used by this driver is described in ACPI v6.4 section 12, "ACPI Embedded Controller
// Interface Specification".
class EcDevice;
using DeviceType = ddk::Device<EcDevice, ddk::Unbindable>;
class EcDevice : public DeviceType {
 public:
  EcDevice(zx_device_t* parent, acpi::Acpi* acpi, ACPI_HANDLE handle,
           std::unique_ptr<IoPortInterface> interface)
      : DeviceType(parent), acpi_(acpi), handle_(handle), io_ports_(std::move(interface)) {}
  static zx_status_t Create(zx_device_t* parent, acpi::Acpi* acpi, ACPI_HANDLE handle);
  zx_status_t Init();
  void DdkRelease() {
    txn_thread_.join();
    query_thread_.join();
    delete this;
  }

  void DdkUnbind(ddk::UnbindTxn txn);

  // Space request handler.
  ACPI_STATUS SpaceRequest(uint32_t function, ACPI_PHYSICAL_ADDRESS addr, uint32_t width,
                           UINT64* value);
  // Called when a GPE is triggered.
  void HandleGpe();

  // Write |value| to |addr| on the EC.
  zx_status_t Write(uint8_t addr, uint8_t val);
  // Read |addr| from the EC.
  zx::status<uint8_t> Read(uint8_t addr);
  // Query the EC for pending events, and return the event code.
  zx::status<uint8_t> Query();

 private:
  // Transaction thread. This is the only thread that handles I/O with the EC.
  // There are two exceptions:
  // * The Query thread checks the status register to see if there are more events pending.
  // * The GPE handler (called from an ACPI interrupt thread) checks the status register to
  //   determine which bits to set on |irq_|.
  void TransactionThread();
  // Perform a transaction, only called from the transaction thread.
  zx_status_t DoTransaction(Transaction* txn);

  // Queue a transaction and block until it is complete.
  zx_status_t QueueTransactionAndWait(Transaction* txn) __TA_EXCLUDES(transaction_lock_);

  // Wait for the given signal(s) to be set.
  // Returns which signals were set, or ZX_ERR_CANCELED if the driver is shutting down.
  zx::status<zx_signals_t> WaitForIrq(zx_signals_t signals);

  // This thread watches for kPendingEvent on |irq_| and then queues queries until SCI_EVT becomes
  // unset.
  void QueryThread();

  // Returns true if we need to acquire the global lock when interacting with the EC.
  zx::status<bool> NeedsGlobalLock();
  // Returns information about the GPE we use.
  zx::status<std::pair<ACPI_HANDLE, uint32_t>> GetGpeInfo();
  // Configures and maps I/O ports.
  zx::status<> SetupIo();

  static uint32_t GpeHandlerThunk(ACPI_HANDLE, uint32_t, void* ctx) {
    static_cast<EcDevice*>(ctx)->HandleGpe();
    return ACPI_REENABLE_GPE;
  }
  static ACPI_STATUS AddressSpaceThunk(uint32_t func, ACPI_PHYSICAL_ADDRESS addr, uint32_t width,
                                       UINT64* value, void* handler_ctx, void* region_ctx) {
    return static_cast<EcDevice*>(handler_ctx)->SpaceRequest(func, addr, width, value);
  }

  std::mutex transaction_lock_;
  std::vector<Transaction*> transaction_queue_ __TA_GUARDED(transaction_lock_);
  std::thread txn_thread_;
  std::thread query_thread_;

  acpi::Acpi* acpi_;
  ACPI_HANDLE handle_;

  uint16_t data_port_ = 0;
  uint16_t cmd_port_ = 0;
  std::unique_ptr<IoPortInterface> io_ports_;
  bool use_global_lock_ = false;
  zx::event irq_;
  std::pair<ACPI_HANDLE, uint32_t> gpe_info_;
  inspect::Inspector inspect_;
  inspect::UintProperty finished_txns_ = inspect_.GetRoot().CreateUint("finished-txns", 0);
  inspect::StringProperty last_query_ = inspect_.GetRoot().CreateString("last-query", "N/A");
};

}  // namespace acpi_ec
