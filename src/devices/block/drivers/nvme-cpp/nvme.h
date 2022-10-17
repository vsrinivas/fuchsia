// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_NVME_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_NVME_H_

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/irq.h>
#include <lib/ddk/io-buffer.h>
#include <lib/device-protocol/pci.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>
#include <ddktl/unbind-txn.h>

#include "src/devices/block/drivers/nvme-cpp/commands.h"
#include "src/devices/block/drivers/nvme-cpp/queue-pair.h"
#include "src/devices/block/drivers/nvme-cpp/queue.h"
#include "src/devices/block/drivers/nvme-cpp/registers.h"

namespace fake_nvme {
class FakeNvmeController;
}

namespace nvme {

class Nvme;
using DeviceType = ddk::Device<Nvme, ddk::Initializable, ddk::Unbindable>;
class Nvme : public DeviceType {
 public:
  explicit Nvme(zx_device_t* parent, ddk::Pci pci, fdf::MmioBuffer buffer)
      : DeviceType(parent), pci_(std::move(pci)), mmio_(std::move(buffer)) {}
  virtual ~Nvme() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // Returns the result of Identify with CNS set to 0.
  // See NVME Command Set Specification 4.1.5, "Identify Command" for more information.
  zx::result<fpromise::promise<Completion, Completion>> IdentifyNamespace(uint32_t id,
                                                                          zx::vmo& data);
  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }
  async::Executor& executor() { return *executor_; }
  uint32_t max_transfer_size() const { return maximum_data_transfer_size_; }

 private:
  friend class NvmeTest;
  friend class fake_nvme::FakeNvmeController;
  // Separate from Bind() so that we can skip it in unit tests.
  zx_status_t InitPciAndDispatcher();
  // Puts a bunch of info from caps_ into inspect.
  void FillInspect();
  // Called by DdkInit(). Asynchronously polls the controller until it enters reset
  // before setting up the admin queues and re-enabling it.
  void ResetAndPrepareQueues(zx::duration waited);
  // Called by ResetAndPrepareQueues. Waits for the controller to leave reset and then queries it
  // to find out about it.
  void WaitForReadyAndStart(zx::duration waited);

  // Enumerate namespaces attached to this controller, and create devices for them.
  void InitializeNamespaces();

  void IrqHandler(async_dispatcher_t* dispatcher, async::IrqBase* irq, zx_status_t status,
                  const zx_packet_interrupt_t* interrupt);

  inspect::Inspector inspect_;
  ddk::Pci pci_;
  zx::bti bti_;
  fdf::MmioBuffer mmio_;
  CapabilityReg caps_;
  VersionReg version_;

  std::optional<ddk::InitTxn> init_txn_;

  // For now, we only have a single I/O completion queue and a single interrupt.
  zx::interrupt irq_;
  async::IrqMethod<Nvme, &Nvme::IrqHandler> irq_handler_{this};
  // MSI-X affects how we mask/unmask interrupts.
  bool is_msix_ = false;

  // Admin queues (completion and submission)
  std::unique_ptr<QueuePair> admin_queue_;
  // IO queues (completion and submission)
  std::unique_ptr<QueuePair> io_queue_;

  fdf::UnownedDispatcher dispatcher_;
  std::unique_ptr<async::Executor> executor_;

  uint32_t maximum_data_transfer_size_ = 0;
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_NVME_H_
