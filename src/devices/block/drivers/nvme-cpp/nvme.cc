// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme-cpp/nvme.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>

#include "src/devices/block/drivers/nvme-cpp/commands/identify.h"
#include "src/devices/block/drivers/nvme-cpp/namespace.h"
#include "src/devices/block/drivers/nvme-cpp/nvme-bind.h"
#include "src/devices/block/drivers/nvme-cpp/registers.h"

namespace nvme {

// c.f. NVMe Base Specification 2.0, section 3.1.3.8 "AQA - Admin Queue Attributes"
constexpr size_t kAdminQueueMaxEntries = 4096;

zx_status_t Nvme::Bind(void* ctx, zx_device_t* dev) {
  auto pci = ddk::Pci::FromFragment(dev);
  if (!pci.is_valid()) {
    zxlogf(ERROR, "Failed to find pci fragment");
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::optional<fdf::MmioBuffer> buffer;
  zx_status_t status = pci.MapMmio(0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get PCI BAR: %s", zx_status_get_string(status));
    return status;
  }

  auto driver = std::make_unique<Nvme>(dev, std::move(pci), std::move(*buffer));
  status = driver->InitPciAndDispatcher();
  if (status != ZX_OK) {
    return status;
  }
  status = driver->Bind();
  if (status != ZX_OK) {
    return status;
  }

  // The DriverFramework now owns driver.
  __UNUSED auto ptr = driver.release();
  return ZX_OK;
}

zx_status_t Nvme::InitPciAndDispatcher() {
  fuchsia_hardware_pci::InterruptMode mode;
  zx_status_t status = pci_.ConfigureInterruptMode(1, &mode);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to configure interrupt: %s", zx_status_get_string(status));
    return status;
  }

  is_msix_ = (mode == fuchsia_hardware_pci::InterruptMode::kMsiX);

  status = pci_.MapInterrupt(0, &irq_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map interrupt: %s", zx_status_get_string(status));
    return status;
  }

  status = pci_.SetBusMastering(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to enable bus mastering: %s", zx_status_get_string(status));
    return status;
  }

  status = pci_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get BTI: %s", zx_status_get_string(status));
    return status;
  }
  // TODO(fxbug.dev/102133): we will probably want our own thread(s) in the future.
  dispatcher_ = fdf::Dispatcher::GetCurrent();
  return ZX_OK;
}

zx_status_t Nvme::Bind() {
  executor_ = std::make_unique<async::Executor>(dispatcher_->async_dispatcher());

  caps_ = CapabilityReg::Get().ReadFrom(&mmio_);
  version_ = VersionReg::Get().ReadFrom(&mmio_);

  if (zx_system_get_page_size() < caps_.memory_page_size_min_bytes()) {
    zxlogf(ERROR, "Page size is too small! (ours: 0x%x, min: 0x%x)", zx_system_get_page_size(),
           caps_.memory_page_size_min_bytes());
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (zx_system_get_page_size() > caps_.memory_page_size_max_bytes()) {
    zxlogf(ERROR, "Page size is too large!");
    return ZX_ERR_NOT_SUPPORTED;
  }

  FillInspect();

  return DdkAdd(ddk::DeviceAddArgs("nvme")
                    .set_inspect_vmo(inspect_.DuplicateVmo())
                    .set_flags(DEVICE_ADD_NON_BINDABLE));
}

constexpr zx::duration kResetPollInterval = zx::msec(1);
void Nvme::ResetAndPrepareQueues(zx::duration waited) {
  if (ControllerStatusReg::Get().ReadFrom(&mmio_).ready()) {
    if (waited >= zx::msec(caps_.timeout_ms())) {
      zxlogf(ERROR, "Reset timed out!");
      init_txn_->Reply(ZX_ERR_TIMED_OUT);
      return;
    }

    waited += kResetPollInterval;
    zx_status_t status = async::PostDelayedTask(
        dispatcher_->async_dispatcher(), [this, waited]() { ResetAndPrepareQueues(waited); },
        kResetPollInterval);
    if (status != ZX_OK) {
      zxlogf(ERROR, "failed to re-post reset task: %s", zx_status_get_string(status));
      init_txn_->Reply(status);
    }
    return;
  }

  // Controller is off, we can set things up.
  // Before we do anything, we need to set up the queues.
  auto admin_queue = QueuePair::Create(bti_.borrow(), 0, kAdminQueueMaxEntries, caps_, mmio_);
  if (admin_queue.is_error()) {
    zxlogf(ERROR, "failed to set up admin queue: %s", admin_queue.status_string());
    init_txn_->Reply(admin_queue.error_value());
    return;
  }
  admin_queue_ = std::move(*admin_queue);

  auto io_queue = QueuePair::Create(bti_.borrow(), 0, caps_.max_queue_entries(), caps_, mmio_);
  if (io_queue.is_error()) {
    zxlogf(ERROR, "failed to set up admin queue: %s", io_queue.status_string());
    init_txn_->Reply(io_queue.error_value());
    return;
  }
  io_queue_ = std::move(*io_queue);

  // Configure the admin queue.
  AdminQueueAttributesReg::Get()
      .ReadFrom(&mmio_)
      .set_completion_queue_size(static_cast<uint32_t>(admin_queue_->completion().entry_count()) -
                                 1)
      .set_submission_queue_size(static_cast<uint32_t>(admin_queue_->submission().entry_count()) -
                                 1)
      .WriteTo(&mmio_);

  AdminQueueAddressReg::CompletionQueue()
      .FromValue(0)
      .set_addr(admin_queue_->completion().GetDeviceAddress())
      .WriteTo(&mmio_);
  AdminQueueAddressReg::SubmissionQueue()
      .FromValue(0)
      .set_addr(admin_queue_->submission().GetDeviceAddress())
      .WriteTo(&mmio_);

  // Write the controller configuration register.
  ControllerConfigReg::Get()
      .ReadFrom(&mmio_)
      .set_controller_ready_independent_of_media(0)
      // Queue entry sizes are powers of two.
      .set_io_completion_queue_entry_size(__builtin_ctzl(sizeof(Completion)))
      .set_io_submission_queue_entry_size(__builtin_ctzl(sizeof(Submission)))
      .set_arbitration_mechanism(ControllerConfigReg::ArbitrationMechanism::kRoundRobin)
      // We know that page size is always at least 4096 (required by spec), and we check
      // that zx_system_get_page_size is supported by the controller in Bind().
      .set_memory_page_size(__builtin_ctzl(zx_system_get_page_size()) - 12)
      .set_io_command_set(ControllerConfigReg::CommandSet::kNvm)
      .set_enabled(1)
      .WriteTo(&mmio_);

  // Timeout may have changed, so double check it.
  caps_.ReadFrom(&mmio_);

  WaitForReadyAndStart(zx::msec(0));
}

void Nvme::WaitForReadyAndStart(zx::duration waited) {
  if (!ControllerStatusReg::Get().ReadFrom(&mmio_).ready()) {
    if (waited > zx::msec(caps_.timeout_ms())) {
      zxlogf(ERROR, "Timed out waiting for controller to leave reset");
      init_txn_->Reply(ZX_ERR_TIMED_OUT);
      return;
    }
    waited += kResetPollInterval;
    zx_status_t status = async::PostDelayedTask(
        dispatcher_->async_dispatcher(), [this, waited]() { WaitForReadyAndStart(waited); },
        kResetPollInterval);

    if (status != ZX_OK) {
      zxlogf(ERROR, "failed to post wait task: %s", zx_status_get_string(status));
      init_txn_->Reply(status);
    }
    return;
  }

  // At this point, the controller is ready, so we set up our interrupt handler and start
  // interrogating it to determine the available storage drives.
  zx_status_t status;
  auto finish = fit::defer([this, &status]() { init_txn_->Reply(status); });
  irq_handler_.set_object(irq_.get());
  status = irq_handler_.Begin(dispatcher_->async_dispatcher());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to listen for IRQ: %s", zx_status_get_string(status));
    return;
  }

  zx::vmo identify_data;
  status = zx::vmo::create(zx_system_get_page_size(), 0, &identify_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to init vmo: %s", zx_status_get_string(status));
    return;
  }

  IdentifySubmission submission;
  submission.set_structure(IdentifySubmission::IdentifyCns::kIdentifyController);

  auto bridge = fpromise::bridge<Completion, Completion>();

  auto identify_status =
      admin_queue_->Submit(submission, identify_data.borrow(), 0, bridge.completer);
  if (identify_status.is_error()) {
    zxlogf(ERROR, "Failed to send identify: %s", identify_status.status_string());
    return;
  }

  finish.cancel();
  executor_->schedule_task(
      bridge.consumer.promise()
          .and_then([vmo = std::move(identify_data), this](Completion& result) {
            fzl::VmoMapper mapper;
            zx_status_t status = mapper.Map(vmo);
            if (status != ZX_OK) {
              init_txn_->Reply(status);
              return;
            }
            IdentifyController* identify = static_cast<IdentifyController*>(mapper.start());
            // Fill in some inspect information
            inspect_.GetRoot().CreateString(
                "serial-no", std::string(identify->serial_number, sizeof(identify->serial_number)),
                &inspect_);
            inspect_.GetRoot().CreateString(
                "model-no", std::string(identify->model_number, sizeof(identify->model_number)),
                &inspect_);
            inspect_.GetRoot().CreateString(
                "fw-rev", std::string(identify->firmware_rev, sizeof(identify->firmware_rev)),
                &inspect_);

            if (identify->minimum_cq_entry_size() != sizeof(Completion) ||
                identify->minimum_sq_entry_size() != sizeof(Submission)) {
              zxlogf(ERROR,
                     "Controller has unexpected cq/sq entry size requirement (cq entry size: %zu, "
                     "sq entry "
                     "size: %zu)",
                     identify->minimum_cq_entry_size(), identify->minimum_sq_entry_size());
              init_txn_->Reply(ZX_ERR_NOT_SUPPORTED);
              return;
            }
            zxlogf(INFO, "Maximum commands: %u", identify->max_cmd);
            zxlogf(INFO, "number of namespaces: %u", identify->num_namespaces);
            if (identify->max_data_transfer != 0) {
              maximum_data_transfer_size_ =
                  static_cast<uint32_t>((1 << identify->max_data_transfer)) *
                  caps_.memory_page_size_min_bytes();
            }
            zxlogf(INFO, "max data transfer size: %u bytes", maximum_data_transfer_size_);
            init_txn_->Reply(ZX_OK);

            InitializeNamespaces();
          })
          .or_else([this](Completion& result) {
            zxlogf(ERROR, "Identify failed: type=%d code=%d", result.status_code_type(),
                   result.status_code());
            init_txn_->Reply(ZX_ERR_INTERNAL);
          }));
}

void Nvme::InitializeNamespaces() {
  zx::vmo identify_data;
  zx_status_t status = zx::vmo::create(zx_system_get_page_size(), 0, &identify_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to init vmo: %s", zx_status_get_string(status));
    return;
  }

  IdentifySubmission submission;
  submission.set_structure(IdentifySubmission::IdentifyCns::kActiveNamespaceList);

  auto bridge = fpromise::bridge<Completion, Completion>();
  auto identify_status =
      admin_queue_->Submit(submission, identify_data.borrow(), 0, bridge.completer);
  if (identify_status.is_error()) {
    zxlogf(ERROR, "Failed to submit identify active namespaces command: %s",
           identify_status.status_string());
    return;
  }

  executor_->schedule_task(
      bridge.consumer.promise()
          .and_then([this, vmo = std::move(identify_data)](Completion& result) {
            fzl::VmoMapper mapper;
            zx_status_t status = mapper.Map(vmo);
            if (status != ZX_OK) {
              zxlogf(ERROR, "Failed to map namespaces VMO: %s", zx_status_get_string(status));
              return;
            }

            IdentifyActiveNamespaces* ns = static_cast<IdentifyActiveNamespaces*>(mapper.start());
            for (size_t i = 0; i < std::size(ns->nsid) && ns->nsid[i] != 0; i++) {
              status = Namespace::Create(this, ns->nsid[i]);
              if (status != ZX_OK) {
                zxlogf(WARNING, "Failed to add namespace %u: %s", ns->nsid[i],
                       zx_status_get_string(status));
              }
            }
          })
          .or_else([](Completion& result) {
            zxlogf(ERROR, "Failed to get namespace list Status type=0x%x code=0x%x",
                   result.status_code_type(), result.status_code());
          }));
}

zx::result<fpromise::promise<Completion, Completion>> Nvme::IdentifyNamespace(uint32_t id,
                                                                              zx::vmo& data) {
  IdentifySubmission submission;
  submission.set_opcode(IdentifySubmission::kOpcode);
  submission.namespace_id = id;
  submission.set_structure(IdentifySubmission::IdentifyCns::kIdentifyNamespace);

  auto bridge = fpromise::bridge<Completion, Completion>();
  auto identify_status = admin_queue_->Submit(submission, data.borrow(), 0, bridge.completer);
  if (identify_status.is_error()) {
    zxlogf(ERROR, "Failed to submit identify namespace command: %s",
           identify_status.status_string());
    return identify_status.take_error();
  }

  return zx::ok(bridge.consumer.promise());
}

void Nvme::DdkInit(ddk::InitTxn txn) {
  init_txn_ = std::move(txn);
  // Reset the controller
  if (ControllerStatusReg::Get().ReadFrom(&mmio_).ready()) {
    zxlogf(INFO, "Controller is already active, resetting it.");
    ControllerConfigReg::Get().ReadFrom(&mmio_).set_enabled(0).WriteTo(&mmio_);
  }

  zx_status_t status = async::PostTask(dispatcher_->async_dispatcher(),
                                       [this]() { ResetAndPrepareQueues(zx::msec(0)); });
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to post reset task: %s", zx_status_get_string(status));
    init_txn_->Reply(status);
  }
}

void Nvme::IrqHandler(async_dispatcher_t* dispatcher, async::IrqBase* irq, zx_status_t status,
                      const zx_packet_interrupt_t* interrupt) {
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to process interrupt: %s", zx_status_get_string(status));
  }

  // This register is only available when not using MSI-X.
  if (!is_msix_) {
    InterruptReg::MaskSet().FromValue(1).WriteTo(&mmio_);
  }

  async::PostTask(dispatcher_->async_dispatcher(), [this]() {
    // Check queues to see what triggered the IRQ.
    admin_queue_->CheckForNewCompletions();
    io_queue_->CheckForNewCompletions();

    if (is_msix_) {
      irq_.ack();
    } else {
      // Unmask the interrupt
      InterruptReg::MaskClear().FromValue(1).WriteTo(&mmio_);
    }
  });

  // Ack the interrupt now if we're not using MSI-X.
  // If we are using MSI-X, we leave it unacked (and masked) until we're finished checking for
  // completions.
  if (!is_msix_) {
    irq_.ack();
  }
}

void Nvme::DdkUnbind(ddk::UnbindTxn txn) {
  async::PostTask(dispatcher_->async_dispatcher(), [this, txn = std::move(txn)]() mutable {
    // Destroy the executor from the dispatcher.
    // We need to do this because otherwise the async::Executor might still be running a task on
    // the dispatcher, which would hold a reference to the executor (that's been destroyed).
    executor_.reset();
    // TODO(fxb/103753): Currently the runtime dispatcher expects the interrupt to be cancelled from
    // the synchronized dispatcher thread.
    irq_handler_.Cancel();
    txn.Reply();
  });
}

void Nvme::DdkRelease() { delete this; }

void Nvme::FillInspect() {
  zxlogf(INFO, "NVMe version %d.%d.%d", version_.major(), version_.minor(), version_.tertiary());
  inspect_.GetRoot().CreateInt("version-major", version_.major(), &inspect_);
  inspect_.GetRoot().CreateInt("version-minor", version_.minor(), &inspect_);
  inspect_.GetRoot().CreateInt("version-tertiary", version_.tertiary(), &inspect_);

  auto caps = inspect_.GetRoot().CreateChild("capabilites");

  if (version_ >= VersionReg::FromVer(1, 4, 0)) {
    caps.CreateBool("controller-ready-with-media", caps_.controller_ready_with_media_supported(),
                    &inspect_);
    caps.CreateBool("controller-ready-without-media",
                    caps_.controller_ready_independent_media_supported(), &inspect_);
  }

  caps.CreateBool("nvm-shutdown", caps_.subsystem_shutdown_supported(), &inspect_);
  caps.CreateBool("controller-memory-buffer", caps_.controller_memory_buffer_supported(),
                  &inspect_);
  caps.CreateBool("persistent-memory-region", caps_.persistent_memory_region_supported(),
                  &inspect_);
  caps.CreateInt("memory-page-size-max", caps_.memory_page_size_max_bytes(), &inspect_);
  caps.CreateInt("memory-page-size-min", caps_.memory_page_size_min_bytes(), &inspect_);
  caps.CreateInt("controller-power-scope", caps_.controller_power_scope(), &inspect_);
  caps.CreateBool("boot-partition", caps_.boot_partition_support(), &inspect_);
  caps.CreateBool("no-io-command-set", caps_.no_io_command_set_support(), &inspect_);
  caps.CreateBool("identify-io-command-set", caps_.identify_io_command_set_support(), &inspect_);
  caps.CreateBool("nvm-command-set", caps_.nvm_command_set_support(), &inspect_);
  caps.CreateBool("nvm-subsystem-reset", caps_.nvm_subsystem_reset_supported(), &inspect_);
  caps.CreateInt("doorbell-stride", caps_.doorbell_stride_bytes(), &inspect_);
  // TODO(fxbug.dev/102133): interpret CRTO register if version > 1.4
  caps.CreateInt("ready-timeout-ms", caps_.timeout_ms(), &inspect_);
  caps.CreateBool("vendor-specific-arbitration", caps_.vendor_specific_arbitration_supported(),
                  &inspect_);
  caps.CreateBool("weighted-round-robin-arbitration",
                  caps_.weighted_round_robin_arbitration_supported(), &inspect_);
  caps.CreateBool("contiguous-queue-required", caps_.contiguous_queues_required(), &inspect_);
  caps.CreateInt("maximum-queue-entries", caps_.max_queue_entries(), &inspect_);

  inspect_.emplace(std::move(caps));
}

static zx_driver_ops_t nvme_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Nvme::Bind;
  return ops;
}();

}  // namespace nvme

ZIRCON_DRIVER(Nvme, nvme::nvme_driver_ops, "zircon", "0.1");
