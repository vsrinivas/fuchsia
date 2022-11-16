// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>

#include "src/devices/block/drivers/nvme/queue-pair.h"

namespace nvme {

struct nvme_device_t;
struct IoCommand;

class Nvme;
using DeviceType = ddk::Device<Nvme, ddk::Initializable>;
class Nvme : public DeviceType, public ddk::BlockImplProtocol<Nvme, ddk::base_protocol> {
 public:
  explicit Nvme(zx_device_t* parent) : DeviceType(parent) {}
  ~Nvme() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t AddDevice(zx_device_t* dev);

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // BlockImpl implementations
  void BlockImplQuery(block_info_t* out_info, uint64_t* out_block_op_size);
  void BlockImplQueue(block_op_t* txn, block_impl_queue_callback callback, void* cookie);

 private:
  static int IoThread(void* arg) { return static_cast<Nvme*>(arg)->IoLoop(); }
  static int IrqThread(void* arg) { return static_cast<Nvme*>(arg)->IrqLoop(); }
  int IoLoop();
  int IrqLoop();

  // Main driver initialization.
  zx_status_t Init();

  // Perform an admin command synchronously (i.e., blocks for the command to complete or timeout).
  zx_status_t DoAdminCommandSync(Submission& submission,
                                 std::optional<zx::unowned_vmo> admin_data = std::nullopt);

  // Attempt to submit NVMe transactions for an IoCommand. Returns false if this could not be
  // completed due to temporary lack of resources, or true if either it succeeded or errored out.
  bool SubmitAllTxnsForIoCommand(IoCommand* io_cmd);

  // Process pending IO commands. Called in the IoLoop().
  void ProcessIoSubmissions();
  // Process pending IO completions. Called in the IoLoop().
  void ProcessIoCompletions();

  pci_protocol_t pci_;
  std::unique_ptr<fdf::MmioBuffer> mmio_;
  zx_handle_t irqh_;
  zx::bti bti_;
  CapabilityReg caps_;
  VersionReg version_;

  fbl::Mutex commands_lock_;
  // The pending list consists of commands that have been received via BlockImplQueue() and are
  // waiting for IO to start. The exception is the head of the pending list which may be partially
  // started, waiting for more txn slots to become available.
  list_node_t pending_commands_ TA_GUARDED(commands_lock_);  // Inbound commands to process.
  // The active list consists of commands where all txns have been created and we're waiting for
  // them to complete or error out.
  list_node_t active_commands_ TA_GUARDED(commands_lock_);  // Commands in flight.

  // Admin submission and completion queues.
  std::unique_ptr<QueuePair> admin_queue_;
  fbl::Mutex admin_lock_;  // Used to serialize admin transactions.
  sync_completion_t admin_signal_;
  Completion admin_result_;

  // IO submission and completion queues.
  std::unique_ptr<QueuePair> io_queue_;
  // Notifies IoThread() that it has work to do. Signaled from BlockImplQueue() or IrqThread().
  sync_completion_t io_signal_;

  // Interrupt and IO threads.
  thrd_t irq_thread_;
  thrd_t io_thread_;
  bool irq_thread_started_ = false;
  bool io_thread_started_ = false;

  bool volatile_write_cache_ = false;
  bool driver_shutdown_ = false;

  block_info_t block_info_;
  uint32_t max_transfer_blocks_;
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_
