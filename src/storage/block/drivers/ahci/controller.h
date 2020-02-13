// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_AHCI_CONTROLLER_H_
#define SRC_STORAGE_BLOCK_DRIVERS_AHCI_CONTROLLER_H_

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

#include "ahci.h"
#include "bus.h"
#include "port.h"

namespace ahci {

struct ThreadWrapper {
  thrd_t thread;
  bool created = false;

  ~ThreadWrapper() { ZX_DEBUG_ASSERT(created == false); }

  zx_status_t CreateWithName(thrd_start_t entry, void* arg, const char* name) {
    ZX_DEBUG_ASSERT(created == false);
    if (thrd_create_with_name(&thread, entry, arg, name) == thrd_success) {
      created = true;
      return ZX_OK;
    }
    return ZX_ERR_NO_MEMORY;
  }

  void Join() {
    if (!created)
      return;
    thrd_join(thread, nullptr);
    created = false;
  }
};

class Controller {
 public:
  Controller() {}
  ~Controller();

  DISALLOW_COPY_ASSIGN_AND_MOVE(Controller);

  // Create a new AHCI Controller.
  static zx_status_t Create(zx_device_t* parent, std::unique_ptr<Controller>* con_out);

  // Test function: Create a new Controller with a caller-provided host bus interface.
  static zx_status_t CreateWithBus(zx_device_t* parent, std::unique_ptr<Bus> bus,
                                   std::unique_ptr<Controller>* con_out);

  // Release call for device protocol. Calls Shutdown and deletes this Controller.
  static void Release(void* ctx);

  // Read or write a 32-bit AHCI controller reg. Endinaness is corrected.
  uint32_t RegRead(size_t offset);
  zx_status_t RegWrite(size_t offset, uint32_t val);

  static int WorkerThread(void* arg) { return static_cast<Controller*>(arg)->WorkerLoop(); }

  static int IrqThread(void* arg) { return static_cast<Controller*>(arg)->IrqLoop(); }

  static int InitThread(void* arg) { return static_cast<Controller*>(arg)->InitScan(); }

  // Create worker, irq, and watchdog threads.
  zx_status_t LaunchThreads();

  // Release all resources.
  // Not used in DDK lifecycle where Release() is called.
  void Shutdown() __TA_EXCLUDES(lock_);

  zx_status_t HbaReset();
  void AhciEnable();

  zx_status_t SetDevInfo(uint32_t portnr, sata_devinfo_t* devinfo);
  void Queue(uint32_t portnr, sata_txn_t* txn);

  void SignalWorker() { sync_completion_signal(&worker_completion_); }

  Bus* bus() { return bus_.get(); }
  zx_device_t** zxdev_ptr() { return &zxdev_; }

 private:
  int WorkerLoop();
  int WatchdogLoop();
  int IrqLoop();
  int InitScan();

  bool ShouldExit() __TA_EXCLUDES(lock_);

  zx_device_t* zxdev_ = nullptr;
  uint32_t cap_ = 0;

  fbl::Mutex lock_;
  bool threads_should_exit_ __TA_GUARDED(lock_) = false;

  ThreadWrapper irq_thread_;
  ThreadWrapper worker_thread_;

  sync_completion_t worker_completion_;

  std::unique_ptr<Bus> bus_;
  Port ports_[AHCI_MAX_PORTS];
};

}  // namespace ahci

#endif  // SRC_STORAGE_BLOCK_DRIVERS_AHCI_CONTROLLER_H_
