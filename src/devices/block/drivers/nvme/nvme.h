// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio-buffer.h>

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

  // Attempt to submit NVMe transactions for an IoCommand. Returns false if this could not be
  // completed due to temporary lack of resources, or true if either it succeeded or errored out.
  bool SubmitAllTxnsForIoCommand(IoCommand* io_cmd);

  // Process pending IO commands. Called in the IoLoop().
  void ProcessIoSubmissions();
  // Process pending IO completions. Called in the IoLoop().
  void ProcessIoCompletions();

  // TODO(fxbug.dev/102133): Extract variables from this struct as class members.
  nvme_device_t* nvme_;

  pci_protocol_t pci_;
  std::unique_ptr<fdf::MmioBuffer> mmio_;
  zx_handle_t irqh_;
  zx::bti bti_;
  CapabilityReg caps_;
  VersionReg version_;

  // IO submission and completion queues.
  std::unique_ptr<QueuePair> io_queue_;

  // Interrupt and IO threads.
  thrd_t irq_thread_;
  thrd_t io_thread_;
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_
