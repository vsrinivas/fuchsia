// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/gvnic/gvnic.h"

#include "src/connectivity/ethernet/drivers/gvnic/abi.h"
#include "src/connectivity/ethernet/drivers/gvnic/gvnic-bind.h"

#define DIV_ROUND_UP(a, b) (((a) + (b)-1) / (b))
#define ROUND_UP(a, b) (DIV_ROUND_UP(a, b) * b)

#define SET_LOCAL_REG_AND_WRITE_TO_MMIO(f, v)                                       \
  do {                                                                              \
    regs_.f = (v);                                                                  \
    reg_mmio_->Write<decltype(regs_.f)::wrapped_type>(regs_.f.GetBE(),              \
                                                      offsetof(GvnicRegisters, f)); \
  } while (0)

#define READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(f) \
  (regs_.f.SetBE(reg_mmio_->Read<decltype(regs_.f)::wrapped_type>(offsetof(GvnicRegisters, f))))

#define POLL_AT_MOST_N_TIMES_UNTIL_CONDITION_IS_MET(n, cond, msg) \
  do {                                                            \
    uint32_t loop_limit = n;                                      \
    while (unlikely(!(cond))) {                                   \
      ZX_ASSERT_MSG(loop_limit--, "Polling timed out for " msg);  \
      sched_yield();                                              \
      zxlogf(DEBUG, "Polling for " msg);                          \
    }                                                             \
  } while (0)

#define CACHELINE_SIZE (64)

namespace gvnic {

zx_status_t Gvnic::Bind(void* ctx, zx_device_t* dev) {
  zx_status_t status;

  auto driver = std::make_unique<Gvnic>(dev);
  status = driver->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't bind: %s", zx_status_get_string(status));
    return status;
  }

  // The DriverFramework now owns driver.
  __UNUSED auto ptr = driver.release();

  zxlogf(INFO, "Succeess.");

  return ZX_OK;
}

zx_status_t Gvnic::Bind() {
  zx_status_t status;

  status = SetUpPci();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't set up PCI: %s", zx_status_get_string(status));
    return status;
  }
  status = MapBars();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't map bars: %s", zx_status_get_string(status));
    return status;
  }
  status = ResetCard(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't reset card: %s", zx_status_get_string(status));
    return status;
  }
  static constexpr const char* const version_string = "Fuchsia gvnic driver " GVNIC_VERSION;
  status = WriteVersion(version_string, strlen(version_string));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't write version: %s", zx_status_get_string(status));
    return status;
  }
  status = CreateAdminQueue();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't create admin queue: %s", zx_status_get_string(status));
    return status;
  }
  status = EnableCard();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't enable card: %s", zx_status_get_string(status));
    return status;
  }
  status = DescribeDevice();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't describe device: %s", zx_status_get_string(status));
    return status;
  }
  status = ReportLinkSpeed();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't report link speed: %s", zx_status_get_string(status));
    return status;
  }
  status = ConfigureDeviceResources();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't configure device resources: %s", zx_status_get_string(status));
    return status;
  }
  status = RegisterPageList(tx_page_list_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't register tx page list: %s", zx_status_get_string(status));
    return status;
  }
  status = RegisterPageList(rx_page_list_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't register rx page list: %s", zx_status_get_string(status));
    return status;
  }
  status = CreateTXQueue();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't create tx queue: %s", zx_status_get_string(status));
    return status;
  }
  status = CreateRXQueue();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't create rx queue: %s", zx_status_get_string(status));
    return status;
  }
  status = StartRXThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't start rx thread: %s", zx_status_get_string(status));
    return status;
  }
  status = DdkAdd(ddk::DeviceAddArgs("gvnic").set_inspect_vmo(inspect_.DuplicateVmo()));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't add inspect vmo: %s", zx_status_get_string(status));
    return status;
  }
  is_bound.Set(true);

  return ZX_OK;
}

zx_status_t Gvnic::SetUpPci() {
  zx_status_t status;

  pci_ = ddk::Pci::FromFragment(parent());
  if (!pci_.is_valid()) {
    zxlogf(ERROR, "Couldn't create pci object from parent fragment");
    return ZX_ERR_INTERNAL;
  }
  status = pci_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't get bti from pci: %s", zx_status_get_string(status));
    return status;
  }
  status = pci_.SetBusMastering(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't set pci bus mastering: %s", zx_status_get_string(status));
    return status;
  }
  status = pci_.SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kMsiX, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't set pci interrupt mode to msix: %s", zx_status_get_string(status));
    return status;
  }
  buffer_factory_ = dma_buffer::CreateBufferFactory();
  return ZX_OK;
}

zx_status_t Gvnic::MapBars() {
  zx_status_t status;

  status = pci_.MapMmio(GVNIC_REGISTER_BAR, ZX_CACHE_POLICY_UNCACHED_DEVICE, &reg_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't map mmio for the regiter bar: %s", zx_status_get_string(status));
    return status;
  }
  status = pci_.MapMmio(GVNIC_DOORBELL_BAR, ZX_CACHE_POLICY_UNCACHED_DEVICE, &doorbell_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't map mmio for the doorbell bar: %s", zx_status_get_string(status));
    return status;
  }
  // Initialize the local copies of regs_.
  memset(&regs_, 0, sizeof(regs_));
  READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(dev_status);
  READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(max_tx_queues);
  READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(max_rx_queues);
  READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(admin_queue_counter);
  READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(dma_mask);
  return ZX_OK;
}

zx_status_t Gvnic::ResetCard(bool use_new_reset_sequence) {
  if (use_new_reset_sequence) {
    // A driver can cause a device reset by writing GVE_RESET to the DRV_STATUS register. Drivers
    // should then poll the DEV_STATUS register until the GVE_IS_RESET bit is set to confirm the AQ
    // was released and the device reset is complete.
    SET_LOCAL_REG_AND_WRITE_TO_MMIO(drv_status, GVNIC_DRIVER_STATUS_RESET);
    // In practice, this "polling" succeeds instantly.
    POLL_AT_MOST_N_TIMES_UNTIL_CONDITION_IS_MET(
        10,
        READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(dev_status) &
            GVNIC_DEVICE_STATUS_DEVICE_IS_RESET,
        "reset completion (new)");
  } else {
    // To be compatible with older driver versions, writing 0x0 to the ADMIN_QUEUE_PFN register will
    // also cause a device reset but all future driver versions should use the DRV_STATUS register.
    // Older drivers using the ADMIN_QUEUE_PFN register should then poll the ADMIN_QUEUE_PFN
    // register until it reads back 0 to confirm the AQ was released and the device reset is
    // complete.
    SET_LOCAL_REG_AND_WRITE_TO_MMIO(admin_queue_pfn, 0);
    // In practice, this "polling" succeeds instantly.
    POLL_AT_MOST_N_TIMES_UNTIL_CONDITION_IS_MET(
        10, READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(admin_queue_pfn) == 0,
        "reset completion (old)");
  }
  // Now that we know the device is not using the admin queue (or any other physical addresses) we
  // can release any quarentined pages.
  bti_.release_quarantine();
  return ZX_OK;
}

zx_status_t Gvnic::WriteVersion(const char* ver, uint32_t len) {
  // DRIVER_VERSION Register:
  // Drivers should identify themselves by writing a human readable version string to this register
  // one byte at a time, followed by a newline. After each write, a driver can poll the register and
  // wait for it to read 0 before writing it again. If the driver does not poll and wait, then it is
  // not guaranteed that the driver version will be correctly processed by the device.

  while (len--) {
    // Write one byte
    SET_LOCAL_REG_AND_WRITE_TO_MMIO(driver_version, *ver++);
    // Poll until the byte has been "digested", indicated by reading back a 0.
    // In practice, this "polling" succeeds instantly.
    POLL_AT_MOST_N_TIMES_UNTIL_CONDITION_IS_MET(
        10, READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(driver_version) == 0,
        "version byte write confirmation.");
  }
  // Terminate by writing the newline.
  SET_LOCAL_REG_AND_WRITE_TO_MMIO(driver_version, '\n');
  // Not bothering to wait for the newline to be digested, because there is no benefit.
  return ZX_OK;
}

zx_status_t Gvnic::CreateAdminQueue() {
  zx_status_t status;

  ZX_ASSERT_MSG(!admin_queue_, "Admin Queue alredy allocated.");
  status = buffer_factory_->CreateContiguous(bti_, zx_system_get_page_size(), 0, &admin_queue_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't create admin queue: %s", zx_status_get_string(status));
    return status;
  }
  SET_LOCAL_REG_AND_WRITE_TO_MMIO(admin_queue_base_address, admin_queue_->phys());
  SET_LOCAL_REG_AND_WRITE_TO_MMIO(admin_queue_length, GVNIC_ADMINQ_SIZE);
  admin_queue_index_ = 0;
  admin_queue_num_allocated_ = 0;
  ZX_ASSERT_MSG(!scratch_page_, "Scratch page alredy allocated.");
  status = buffer_factory_->CreateContiguous(bti_, zx_system_get_page_size(), 0, &scratch_page_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't create scratch page: %s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t Gvnic::EnableCard() {
  SET_LOCAL_REG_AND_WRITE_TO_MMIO(drv_status, GVNIC_DRIVER_STATUS_RUN);
  return ZX_OK;
}

zx_status_t Gvnic::DescribeDevice() {
  ZX_ASSERT_MSG(scratch_page_, "Scratch page not allocated.");
  GvnicAdminqEntry* ent = NextAdminQEntry();
  ent->opcode = GVNIC_ADMINQ_DESCRIBE_DEVICE;
  ent->status = 0x0;  // Device will set this when the command completes.
  ent->describe_device.device_descriptor_addr = scratch_page_->phys();
  ent->describe_device.device_descriptor_version = 1;
  ent->describe_device.available_length = static_cast<uint32_t>(scratch_page_->size());
  ZX_ASSERT_MSG(ent->describe_device.available_length == scratch_page_->size(),
                "length did not fit in uint32_t");
  SubmitPendingAdminQEntries(true);
  if (ent->status != GVNIC_ADMINQ_STATUS_PASSED) {
    zxlogf(ERROR, "Describe device command status is not 'passed'");
    return ZX_ERR_INTERNAL;
  }
  auto volatile dev_descr_in_scratch =
      reinterpret_cast<GvnicDeviceDescriptor*>(scratch_page_->virt());
  dev_descr_ = *dev_descr_in_scratch;

  const uint64_t max_options =
      (scratch_page_->size() - sizeof(dev_descr_in_scratch[0])) / sizeof(GvnicDeviceOption);
  if (dev_descr_.num_device_options >= max_options) {
    zxlogf(WARNING,
           "# of device options (%hu) is >= max_options (%lu). Options might be incomplete.",
           dev_descr_.num_device_options.val(), max_options);
  }
  dev_opts_.reset(new GvnicDeviceOption[dev_descr_.num_device_options]);
  auto options_in_scratch = reinterpret_cast<GvnicDeviceOption*>(&dev_descr_in_scratch[1]);

  for (int o = 0; o < dev_descr_.num_device_options; o++) {
    dev_opts_[o] = options_in_scratch[o];
  }
  return ZX_OK;
}

zx_status_t Gvnic::ReportLinkSpeed() {
  ZX_ASSERT_MSG(scratch_page_, "Scratch page not allocated.");
  GvnicAdminqEntry* ent = NextAdminQEntry();
  ent->opcode = GVNIC_ADMINQ_REPORT_LINK_SPEED;
  ent->status = 0x0;  // Device will set this when the command completes.
  ent->report_link_speed.link_speed_address = scratch_page_->phys();
  SubmitPendingAdminQEntries(true);
  if (ent->status != GVNIC_ADMINQ_STATUS_PASSED) {
    zxlogf(ERROR, "Report link speed command status is not 'passed'");
    return ZX_ERR_INTERNAL;
  }

  auto volatile const ls_ptr = reinterpret_cast<GvnicAdminqLinkSpeed*>(scratch_page_->virt());
  link_speed_ = ls_ptr->link_speed;
  return ZX_OK;
}

zx_status_t Gvnic::ConfigureDeviceResources() {
  zx_status_t status;

  GvnicAdminqEntry* ent = NextAdminQEntry();
  ent->opcode = GVNIC_ADMINQ_CONFIGURE_DEVICE_RESOURCES;
  ent->status = 0x0;  // Device will set this when the command completes.

  ZX_ASSERT_MSG(!counter_page_, "Counter page alredy allocated.");
  status = buffer_factory_->CreateContiguous(bti_, zx_system_get_page_size(), 0, &counter_page_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't create counter page: %s", zx_status_get_string(status));
    return status;
  }
  if (!irq_doorbell_idx_page_) {
    status = buffer_factory_->CreateContiguous(bti_, zx_system_get_page_size(), 0,
                                               &irq_doorbell_idx_page_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Couldn't create irq doorbell index page: %s", zx_status_get_string(status));
      return status;
    }
  }
  const uint32_t num_counters = 2;

  num_irq_doorbell_idxs_ = 1;
  next_doorbell_idx_ = 0;
  max_doorbells_ = num_irq_doorbell_idxs_ + num_counters;

  // The stride sets the distance between consecutive doorbell entries. This can be used to space
  // them out (for example, to keep each doorbell in its own cacheline).
  irq_doorbell_idx_stride_ = 16;  // Irrelevant, since there is only one... no "between" at all.

  ent->device_resources.counter_array = counter_page_->phys();
  ent->device_resources.irq_db_addr_base = irq_doorbell_idx_page_->phys();

  // The IRQ doorbells themselves are in BAR2. The IRQ doorbell indexes are filled in to indicate
  // which ones are used by IRQs (and therefore cannot be used by TX or RX queues). For the TX
  // queue(s), the doorbell is incremented for each unit of data (descriptor) to be sent, and the
  // corresponding counter acks that the transmit is complete, and the buffer(s) can be reused. For
  // RX queue(s), the doorbell is incremented for each "empty" packet buffer provided, and the
  // counter indicates when they have been filled.
  ent->device_resources.num_counters = num_counters;  // 1024 allocated, but using 2: 1 TX and 1 RX
  ent->device_resources.num_irq_dbs = num_irq_doorbell_idxs_;
  ent->device_resources.irq_db_stride = sizeof(uint32_t) * irq_doorbell_idx_stride_;

  // From the docs:
  //
  // If ntfy_blk_msix_base_idx is 0, that means that the ntfy blocks start at the first (0 index)
  // interrupt, making the management interrupt the last interrupt. If ntfy_blk_msix_base_idx is 1,
  // that means that the ntfy blocks start at the second (1 index) interrupt, making the management
  // interrupt the first interrupt.
  ent->device_resources.ntfy_blk_msix_base_idx = 0;
  ent->device_resources.queue_format = GVNIC_GQI_QPL_FORMAT;

  SubmitPendingAdminQEntries(true);
  if (ent->status != GVNIC_ADMINQ_STATUS_PASSED) {
    zxlogf(ERROR, "Configure device resources command status is not 'passed'");
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t Gvnic::RegisterPageList(std::unique_ptr<PageList>& page_list) {
  ZX_ASSERT_MSG(scratch_page_, "Scratch page not allocated.");
  page_list.reset(new PageList(buffer_factory_, bti_, scratch_page_));

  GvnicAdminqEntry* ent = NextAdminQEntry();
  ent->opcode = GVNIC_ADMINQ_REGISTER_PAGE_LIST;
  ent->status = 0x0;  // Device will set this when the command completes.

  ent->register_page_list.page_list_id = page_list->id();
  ent->register_page_list.num_pages = page_list->length();
  ent->register_page_list.page_list_address = scratch_page_->phys();
  ent->register_page_list.page_size = scratch_page_->size();

  SubmitPendingAdminQEntries(true);
  if (ent->status != GVNIC_ADMINQ_STATUS_PASSED) {
    zxlogf(ERROR, "Register page list command status is not 'passed'");
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t Gvnic::CreateTXQueue() {
  zx_status_t status;

  GvnicAdminqEntry* ent = NextAdminQEntry();
  ent->opcode = GVNIC_ADMINQ_CREATE_TX_QUEUE;
  ent->status = 0x0;  // Device will set this when the command completes.

  const uint32_t qr_idx = GetNextQueueResourcesIndex();
  tx_queue_resources_ = GetQueueResourcesVirtAddr(qr_idx);

  if (!tx_ring_) {
    status = buffer_factory_->CreateContiguous(bti_, zx_system_get_page_size(), 0, &tx_ring_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Couldn't create tx ring: %s", zx_status_get_string(status));
      return status;
    }
  }
  const auto full_len = tx_ring_->size() / sizeof(GvnicTxPktDesc);
  tx_ring_len_ = static_cast<uint16_t>(full_len);
  ZX_ASSERT_MSG(tx_ring_len_ == full_len, "tx_ring_len_ did not fit in uint16_t");

  ent->create_tx_queue.queue_id = 0;  // Only making one TX Queue.
  ent->create_tx_queue.queue_resources_addr = GetQueueResourcesPhysAddr(qr_idx);
  ent->create_tx_queue.tx_ring_addr = tx_ring_->phys();
  ent->create_tx_queue.queue_page_list_id = tx_page_list_->id();
  // TODO(https://fxbug.dev/107758): To prevent the TX interrupt from waking the RX thread, create a
  // second interrupt, pass its id here, and then mask it.
  ent->create_tx_queue.ntfy_id = 0;            // RX and TX queue both share the same interrupt.
  ent->create_tx_queue.tx_comp_ring_addr = 0;  // Not relevant when in GQI format.
  ent->create_tx_queue.tx_ring_size = tx_ring_len_;
  SubmitPendingAdminQEntries(true);
  if (ent->status != GVNIC_ADMINQ_STATUS_PASSED) {
    zxlogf(ERROR, "Crete tx queue command status is not 'passed'");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t Gvnic::CreateRXQueue() {
  zx_status_t status;

  GvnicAdminqEntry* ent = NextAdminQEntry();
  ent->opcode = GVNIC_ADMINQ_CREATE_RX_QUEUE;
  ent->status = 0x0;  // Device will set this when the command completes.

  const uint32_t qr_idx = GetNextQueueResourcesIndex();
  rx_queue_resources_ = GetQueueResourcesVirtAddr(qr_idx);

  if (!rx_desc_ring_) {  // From NIC
    status = buffer_factory_->CreateContiguous(bti_, zx_system_get_page_size(), 0, &rx_desc_ring_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Couldn't create rx desc ring: %s", zx_status_get_string(status));
      return status;
    }
  }

  if (!rx_data_ring_) {  // To NIC
    status = buffer_factory_->CreateContiguous(bti_, zx_system_get_page_size(), 0, &rx_data_ring_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Couldn't create rx data ring: %s", zx_status_get_string(status));
      return status;
    }
  }

  const auto full_rounded_mtu = ROUND_UP(dev_descr_.mtu + GVNIC_RX_PADDING, CACHELINE_SIZE);
  rounded_mtu_ = static_cast<uint16_t>(full_rounded_mtu);
  ZX_ASSERT_MSG(rounded_mtu_ == full_rounded_mtu, "uint16_t did not fit in uint16_t");

  const auto full_len = rx_desc_ring_->size() / sizeof(GvnicRxDesc);
  rx_ring_len_ = static_cast<uint16_t>(full_len);
  ZX_ASSERT_MSG(rx_ring_len_ == full_len, "rx_ring_len_ did not fit in uint16_t");

  ent->create_rx_queue.queue_id = 0;  // Only making one RX Queue.
  ent->create_rx_queue.slice = 0;     // Obsolete. Not used.
  // TODO(https://fxbug.dev/107758): To prevent the TX interrupt from waking the RX thread, create a
  // second interrupt, pass its id here, and then mask it.
  ent->create_rx_queue.ntfy_id = 0;  // RX and TX queue both share the same interrupt.
  ent->create_rx_queue.queue_resources_addr = GetQueueResourcesPhysAddr(qr_idx);
  ent->create_rx_queue.rx_desc_ring_addr = rx_desc_ring_->phys();
  ent->create_rx_queue.rx_data_ring_addr = rx_data_ring_->phys();
  ent->create_rx_queue.queue_page_list_id = rx_page_list_->id();
  ent->create_rx_queue.rx_ring_size = rx_ring_len_;
  ent->create_rx_queue.packet_buffer_size = rounded_mtu_;
  SubmitPendingAdminQEntries(true);
  if (ent->status != GVNIC_ADMINQ_STATUS_PASSED) {
    zxlogf(ERROR, "Create rx queue command status is not 'passed'");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

GvnicAdminqEntry* Gvnic::NextAdminQEntry() {
  ZX_ASSERT_MSG(admin_queue_num_pending_ + admin_queue_num_allocated_ < GVNIC_ADMINQ_LEN,
                "Ran out of admin queue entries. %u pending, %u allocated, %lu available",
                admin_queue_num_pending_, admin_queue_num_allocated_, GVNIC_ADMINQ_LEN);
  admin_queue_num_allocated_++;
  const uint32_t idx = admin_queue_index_++ % GVNIC_ADMINQ_LEN;
  ZX_ASSERT_MSG(admin_queue_, "Admin queue is not allocated.");
  const auto addr = &(static_cast<GvnicAdminqEntry*>(admin_queue_->virt())[idx]);

  // A driver must zero out any unused portion of the 64-byte command structure. Commands which
  // contain non-zero bytes in unused fields will be rejected by the device.
  memset(addr, 0, sizeof(*addr));
  return addr;
}

void Gvnic::SubmitPendingAdminQEntries(bool wait) {
  ZX_ASSERT_MSG(admin_queue_num_allocated_ > 0, "Cannot submit, %u entries allocated",
                admin_queue_num_allocated_);
  SET_LOCAL_REG_AND_WRITE_TO_MMIO(admin_queue_doorbell, admin_queue_index_);
  admin_queue_num_pending_ += admin_queue_num_allocated_;
  admin_queue_num_allocated_ = 0;
  if (wait) {
    WaitForAdminQueueCompletion();
  }
}

void Gvnic::WaitForAdminQueueCompletion() {
  ZX_ASSERT_MSG(admin_queue_num_pending_ > 0, "Cannot wait for completion, %u pending.",
                admin_queue_num_pending_);
  // In practice, this "polling" succeeds instantly.
  POLL_AT_MOST_N_TIMES_UNTIL_CONDITION_IS_MET(
      10,
      READ_REG_FROM_MMIO_TO_LOCAL_AND_RETURN_VALUE(admin_queue_counter) ==
          regs_.admin_queue_doorbell,
      "admin queue completion.");
  admin_queue_num_pending_ = 0;
}

uint32_t Gvnic::GetNextFreeDoorbellIndex() {
  auto volatile const irq_doorbell_idxs =
      reinterpret_cast<BigEndian<uint32_t>*>(irq_doorbell_idx_page_->virt());

  for (; next_doorbell_idx_ < max_doorbells_; next_doorbell_idx_++) {
    // Cannot just return next_doorbell_idx_ here, because it might already be
    // allocated to an IRQ. Scan through the irq doorbells, and skip to the
    // next one if we collide.
    uint32_t i;
    for (i = 0; i < num_irq_doorbell_idxs_ &&
                irq_doorbell_idxs[i * irq_doorbell_idx_stride_] != next_doorbell_idx_;
         i++)
      ;
    if (i < num_irq_doorbell_idxs_)
      continue;  // Can't use this index, it is in use by IRQ i. Try the next one.
    // No collision found. Fine to return it (and increment for the next call).
    return next_doorbell_idx_++;
  }
  ZX_ASSERT_MSG(false, "This code should be unreachable.");
  return 0xdeadbeef;
}

uint32_t Gvnic::GetNextQueueResourcesIndex() {
  zx_status_t status;
  if (!queue_resources_) {
    next_queue_resources_index_ = 0;
    status =
        buffer_factory_->CreateContiguous(bti_, zx_system_get_page_size(), 0, &queue_resources_);
    if (status != ZX_OK) {
      ZX_ASSERT_MSG(status == ZX_OK, "%s", zx_status_get_string(status));
    }
  }
  const auto num_queue_resources = queue_resources_->size() / sizeof(GvnicQueueResources);
  ZX_ASSERT_MSG(next_queue_resources_index_ < num_queue_resources,
                "Out of queue resources next index (%u) is >= num available (%lu)",
                next_queue_resources_index_, num_queue_resources);
  return next_queue_resources_index_++;
}

zx_paddr_t Gvnic::GetQueueResourcesPhysAddr(uint32_t index) {
  ZX_ASSERT_MSG(queue_resources_,
                "Queue resources is not allocated yet. "
                "How did index %u get allocated without calling GetNextQueueResourcesIndex?",
                index);
  const zx_paddr_t base = queue_resources_->phys();
  // zx_paddr_t is ultimately just an unsigned long. This is normal integer arithmetic, not pointer
  // arithmetic, so we need to multiply by the size of the elements.
  const zx_paddr_t addr = base + (sizeof(GvnicQueueResources) * index);
  return addr;
}

GvnicQueueResources* Gvnic::GetQueueResourcesVirtAddr(uint32_t index) {
  ZX_ASSERT_MSG(queue_resources_,
                "Queue resources is not allocated yet. "
                "How did index %u get allocated without calling GetNextQueueResourcesIndex?",
                index);
  const auto base = reinterpret_cast<GvnicQueueResources*>(queue_resources_->virt());
  return &(base[index]);
}

zx_status_t Gvnic::StartRXThread() {
  if (thrd_create_with_name(
          &rx_thread_, [](void* arg) -> int { return static_cast<Gvnic*>(arg)->RXThread(); }, this,
          "gvnic-rx-thread") != thrd_success) {
    zxlogf(ERROR, "Could not create rx thread");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

void Gvnic::WriteDoorbell(uint32_t index, uint32_t value) {
  const BigEndian<uint32_t> doorbell_val = value;
  doorbell_mmio_->WriteBuffer(sizeof(uint32_t) * index, &doorbell_val, sizeof(uint32_t));
}

uint32_t Gvnic::ReadCounter(uint32_t index) {
  ZX_ASSERT_MSG(scratch_page_, "Scratch page not allocated.");
  const auto volatile counter_base = reinterpret_cast<BigEndian<uint32_t>*>(counter_page_->virt());
  const uint32_t value = counter_base[index];
  return value;
}

// TODO(https://fxbug.dev/107757): Find a clever way to get zerocopy rx to work, and then delete
// this method.
void Gvnic::WritePacketToBufferSpace(const rx_space_buffer_t& buffer, uint8_t* data, uint32_t len) {
  network::SharedAutoLock lock(&vmo_lock_);
  vmo_map_.at(buffer.region.vmo).write(data, buffer.region.offset, len);
}

int Gvnic::RXThread() {
  const auto data_ring_base = reinterpret_cast<BigEndian<uint64_t>*>(rx_data_ring_->virt());
  const auto desc_ring_base = reinterpret_cast<GvnicRxDesc*>(rx_desc_ring_->virt());
  const auto pagelist_base = reinterpret_cast<uint8_t*>(rx_page_list_->pages()->virt());

  ZX_ASSERT_MSG(rounded_mtu_ * rx_ring_len_ < rx_page_list_->pages()->size(),
                "Can't fit %u MTUs (%u bytes each) (= %u bytes) in the page list (%lu bytes)",
                rx_ring_len_, rounded_mtu_, rounded_mtu_ * rx_ring_len_,
                rx_page_list_->pages()->size());
  for (uint32_t i = 0; i < rx_ring_len_; i++) {
    data_ring_base[i] = rounded_mtu_ * i;
  }

  uint32_t ring_doorbell = rx_ring_len_;
  uint32_t ring_counter = 0;
  const uint32_t doorbell_idx = rx_queue_resources_->db_index;
  const uint32_t counter_idx = rx_queue_resources_->counter_index;
  WriteDoorbell(doorbell_idx, ring_doorbell);

  std::vector<rx_buffer_t> completed_rx(rx_ring_len_);
  std::vector<rx_buffer_part_t> completed_rx_parts(rx_ring_len_);

  // Right now, the driver is not using interrupts at all, and instead is just costantly polling.
  // TODO(https://fxbug.dev/107758): Use interrupts when I learn this power. (from a jedi or sith).
  for (;; sched_yield()) {
    const uint32_t counter = ReadCounter(counter_idx);
    uint32_t packets_processed = 0;
    uint32_t packets_written = 0;
    for (; ring_counter != counter; ring_counter++) {
      const uint32_t idx = ring_counter % rx_ring_len_;
      const uint32_t len = desc_ring_base[idx].length - 2;
      uint8_t* const data = pagelist_base + (idx * rounded_mtu_) + 2;
      packets_processed++;

      std::lock_guard rx_lock(rx_queue_lock_);
      if (rx_space_buffer_queue_.Count() == 0) {
        zxlogf(WARNING, "No buffer space. Dropping packet.");
        continue;
      }
      const rx_space_buffer_t& buffer = rx_space_buffer_queue_.Front();
      if (len > buffer.region.length) {
        zxlogf(WARNING,
               "Recieved packet (%u bytes) will not fit in the buffer space (%lu bytes). "
               "Dropping packet.",
               len, buffer.region.length);
        continue;
      }
      completed_rx_parts[packets_written] = {.id = buffer.id, .offset = 0, .length = len};
      completed_rx[packets_written] = {
          .meta =
              {
                  .port = kNetworkPortId,
                  .frame_type =
                      static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
              },
          .data_list = &completed_rx_parts[packets_written],
          .data_count = 1,
      };
      // TODO(https://fxbug.dev/107757): Find a clever way to get zerocopy rx to work, instead of
      // calling this.
      WritePacketToBufferSpace(buffer, data, len);
      rx_space_buffer_queue_.Dequeue();
      packets_written++;
    }
    if (packets_processed) {
      ring_doorbell += packets_processed;
      WriteDoorbell(doorbell_idx, ring_doorbell);
    }
    if (packets_written) {
      network::SharedAutoLock lock(&ifc_lock_);
      ifc_.CompleteRx(completed_rx.data(), packets_written);
    }
  }
  return 0;
}

void Gvnic::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void Gvnic::DdkRelease() { delete this; }

// ------- NetworkDeviceImpl -------
// The quotes in the comments in this section come from the documentation of these fields in
// sdk/banjo/fuchsia.hardware.network.device/network-device.fidl

zx_status_t Gvnic::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  // "`Init` is only called once during the lifetime of the device..."
  static bool called = false;
  ZX_ASSERT_MSG(!called, "NetworkDeviceImplInit already called.");
  called = true;

  fbl::AutoLock lock(&ifc_lock_);
  ifc_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  ifc_.AddPort(kNetworkPortId, this, &network_port_protocol_ops_);
  return ZX_OK;
}

void Gvnic::NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie) {
  if (network_device_impl_started_) {
    callback(cookie, ZX_ERR_ALREADY_BOUND);
    return;
  }
  {
    std::lock_guard tx_lock(rx_queue_lock_);
    rx_space_buffer_queue_.Init(rx_ring_len_);
  }
  {
    std::lock_guard tx_lock(tx_queue_lock_);
    tx_buffer_id_queue_.Init(tx_ring_len_);
  }
  network_device_impl_started_ = true;
  callback(cookie, ZX_OK);
}

void Gvnic::AbortPendingTX() {
  std::lock_guard tx_lock(tx_queue_lock_);
  const uint32_t tx_total_count = tx_buffer_id_queue_.Count();
  tx_result_t completed_tx[tx_total_count];
  for (uint32_t i = 0; i < tx_total_count; i++) {
    completed_tx[i] = {
        .id = tx_buffer_id_queue_.Front(),
        .status = ZX_ERR_BAD_STATE,
    };
    tx_buffer_id_queue_.Dequeue();
  }
  if (tx_total_count) {
    network::SharedAutoLock lock(&ifc_lock_);
    ifc_.CompleteTx(completed_tx, tx_total_count);
  }
  tx_buffer_id_queue_.Init(0);  // Release the queue.
}

void Gvnic::AbortPendingRX() {
  std::lock_guard rx_lock(rx_queue_lock_);
  const uint32_t rx_total_count = rx_space_buffer_queue_.Count();
  std::vector<rx_buffer_t> completed_rx(rx_total_count);
  std::vector<rx_buffer_part_t> completed_rx_parts(rx_total_count);
  for (uint32_t i = 0; i < rx_total_count; i++) {
    completed_rx_parts[i] = {.id = rx_space_buffer_queue_.Front().id};
    completed_rx[i] = {
        .data_list = &completed_rx_parts[i],
        .data_count = 1,
    };
    rx_space_buffer_queue_.Dequeue();
  }
  if (rx_total_count) {
    network::SharedAutoLock lock(&ifc_lock_);
    ifc_.CompleteRx(completed_rx.data(), rx_total_count);
  }
  rx_space_buffer_queue_.Init(0);  // Release the queue.
}

void Gvnic::NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie) {
  ZX_ASSERT_MSG(network_device_impl_started_, "NetworkDeviceImpl not Started.");
  network_device_impl_started_ = false;

  AbortPendingTX();
  AbortPendingRX();
  callback(cookie);
}

void Gvnic::NetworkDeviceImplGetInfo(device_info_t* out_info) {
  memset(out_info, 0, sizeof(*out_info));  // Ensure unset fields are zero.
  out_info->tx_depth = tx_ring_len_;
  out_info->rx_depth = rx_ring_len_;
  // "The typical choice of value is `rx_depth / 2`."
  out_info->rx_threshold = out_info->rx_depth / 2;
  // "Devices that can't perform scatter-gather operations must set `max_buffer_parts` to 1."
  out_info->max_buffer_parts = 1;
  // "Devices that do not support scatter-gather DMA may set this to a value smaller than a page
  // size to guarantee compatibility."
  out_info->max_buffer_length = rounded_mtu_;
  // Don't care because we are not using these buggers for dma, but "Must be greater than zero".
  out_info->buffer_alignment = 1;
  out_info->min_rx_buffer_length = rounded_mtu_;
}

// TODO(https://fxbug.dev/107757): Find a clever way to get zerocopy tx to work, and then delete
// this method.
void Gvnic::WriteBufferToCard(const tx_buffer_t& buffer, uint8_t* data) {
  ZX_ASSERT_MSG(buffer.data_count == 1, "Data count (%lu), should be 1.", buffer.data_count);
  ZX_ASSERT_MSG(buffer.head_length == 0, "Head length (%u) should be 0.", buffer.head_length);
  ZX_ASSERT_MSG(buffer.tail_length == 0, "Tail length (%u) should be 0.", buffer.tail_length);
  network::SharedAutoLock lock(&vmo_lock_);
  const auto len = static_cast<uint32_t>(buffer.data_list[0].length);
  ZX_ASSERT_MSG(len == buffer.data_list[0].length, "Length did not fit in uint32_t");
  vmo_map_.at(buffer.data_list[0].vmo).read(data, buffer.data_list[0].offset, len);
}

void Gvnic::SendTXBuffers(const tx_buffer_t* buf_list, size_t buf_count) {
  auto ring_base = reinterpret_cast<GvnicTxPktDesc*>(tx_ring_->virt());
  auto pagelist_base = reinterpret_cast<uint8_t*>(tx_page_list_->pages()->virt());
  for (uint32_t i = 0; i < buf_count; i++) {
    ZX_ASSERT_MSG(buf_list[i].data_count == 1, "Data count (%lu), should be 1.",
                  buf_list[i].data_count);
    ZX_ASSERT_MSG(buf_list[i].head_length == 0, "Head length (%u) should be 0.",
                  buf_list[i].head_length);
    ZX_ASSERT_MSG(buf_list[i].tail_length == 0, "Tail length (%u) should be 0.",
                  buf_list[i].tail_length);
    uint32_t offset = tx_ring_index_ * rounded_mtu_;
    // TODO(https://fxbug.dev/107757): Find a clever way to get zerocopy tx to work, instead of
    // calling this.
    WriteBufferToCard(buf_list[i], pagelist_base + offset);

    const auto full_len = buf_list[i].data_list[0].length;
    const uint16_t len = static_cast<uint16_t>(full_len);
    ZX_ASSERT_MSG(len == full_len, "len did not fit in uint16_t");

    ring_base[tx_ring_index_] = {
        .type_flags = GVNIC_TXD_STD,
        .checksum_offset = 0,  // "If checksum offload is not requested, the field is Reserved to 0"
        .l4_offset = 0,        // "If checksum offload is not requested, the field is Reserved to 0"
        .descriptor_count = 1,
        .len = len,
        .seg_len = len,
        .seg_addr = offset,
    };
    tx_ring_index_ = (tx_ring_index_ + 1) % tx_ring_len_;
  }
  tx_doorbell_ += buf_count;
  WriteDoorbell(tx_queue_resources_->db_index, tx_doorbell_);
}

void Gvnic::EnqueueTXBuffers(const tx_buffer_t* buf_list, size_t buf_count) {
  std::lock_guard lock(tx_queue_lock_);
  for (uint32_t i = 0; i < buf_count; i++) {
    ZX_ASSERT_MSG(buf_list[i].data_count == 1, "Buffer %u has count %lu (max is 1)", i,
                  buf_list[i].data_count);
    tx_buffer_id_queue_.Enqueue(buf_list[i].id);
  }
}

void Gvnic::FlushTXBuffers() {
  auto get_queue_count = [this] {
    std::lock_guard tx_lock(tx_queue_lock_);
    return tx_buffer_id_queue_.Count();
  };
  const auto initial_queue_count = get_queue_count();
  if (initial_queue_count < (tx_ring_len_ / 2)) {
    // Don't bother to complete the tx buffers until half (or more) are used.
    return;
  }

  do {
    const uint32_t counter = ReadCounter(tx_queue_resources_->counter_index);
    const uint32_t count = counter - tx_counter_;
    if (count == 0) {
      // Nothing to do.
      continue;
    }
    tx_counter_ = counter;

    std::vector<tx_result_t> completed_tx(count);
    for (uint32_t i = 0; i < count; i++) {
      std::lock_guard tx_lock(tx_queue_lock_);
      completed_tx[i] = {
          .id = tx_buffer_id_queue_.Front(),
          .status = ZX_OK,
      };
      tx_buffer_id_queue_.Dequeue();
    }
    network::SharedAutoLock lock(&ifc_lock_);
    ifc_.CompleteTx(completed_tx.data(), count);
    // Don't give up until at least one buffer is available. Yeah, this is pretty ugly, but in
    // practice, this is never actually needed. The card returns completed tx buffers WAY faster
    // than they are sent.
  } while (unlikely(initial_queue_count >= tx_ring_len_ && get_queue_count() >= tx_ring_len_));
}

void Gvnic::NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count) {
  SendTXBuffers(buf_list, buf_count);
  EnqueueTXBuffers(buf_list, buf_count);
  FlushTXBuffers();
}

void Gvnic::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count) {
  std::lock_guard rx_lock(rx_queue_lock_);
  for (uint32_t i = 0; i < buf_count; i++) {
    rx_space_buffer_queue_.Enqueue(buf_list[i]);
  }
}

void Gvnic::NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo,
                                        network_device_impl_prepare_vmo_callback callback,
                                        void* cookie) {
  zx_status_t status = ZX_OK;
  {
    fbl::AutoLock lock(&vmo_lock_);
    if (!vmo_map_.insert({vmo_id, std::move(vmo)}).second) {
      status = ZX_ERR_INTERNAL;
    }
  }
  callback(cookie, status);
}

void Gvnic::NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {
  fbl::AutoLock lock(&vmo_lock_);
  const auto num_erased = vmo_map_.erase(vmo_id);
  ZX_ASSERT_MSG(num_erased == 1, "Expected to erase one vmo, instead erased %lu.", num_erased);
}

void Gvnic::NetworkDeviceImplSetSnoop(bool snoop) {
  // Unimplemented.
}

// ------- NetworkPort -------

void Gvnic::NetworkPortGetInfo(port_info_t* out_info) {
  memset(out_info, 0, sizeof(*out_info));  // Ensure unset fields are zero.

  constexpr uint8_t eth =
      static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet);
  // It expects pointers to a list. These "lists" are only one element long. These are static so
  // that they continue to exist after this function returns.
  static const uint8_t rx_type = eth;
  static const tx_support_t tx_type = {
      .type = eth,
      .features = fuchsia_hardware_network::wire::kFrameFeaturesRaw,
  };

  out_info->port_class = eth;
  out_info->rx_types_list = &rx_type;
  out_info->rx_types_count = 1;
  out_info->tx_types_list = &tx_type;
  out_info->tx_types_count = 1;
}

void Gvnic::NetworkPortGetStatus(port_status_t* out_status) {
  memset(out_status, 0, sizeof(*out_status));  // Ensure unset fields are zero.
  out_status->mtu = dev_descr_.mtu;
  out_status->flags = static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline);
}

void Gvnic::NetworkPortSetActive(bool active) {
  // Nohing to do.
}

void Gvnic::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) {
  memset(out_mac_ifc, 0, sizeof(*out_mac_ifc));
  out_mac_ifc->ops = &mac_addr_protocol_ops_;
  out_mac_ifc->ctx = this;
}

void Gvnic::NetworkPortRemoved() {
  // The port is never removed, there's no extra cleanup needed here.
}

void Gvnic::MacAddrGetAddress(uint8_t* out_mac) { memcpy(out_mac, dev_descr_.mac, ETH_ALEN); }

void Gvnic::MacAddrGetFeatures(features_t* out_features) {
  memset(out_features, 0, sizeof(*out_features));

  // "Implementations must set 0 if multicast filtering is not supported."
  // out_features->multicast_filter_count = 0;
  out_features->supported_modes = MODE_PROMISCUOUS;
}

void Gvnic::MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                           size_t multicast_macs_count) {
  ZX_ASSERT_MSG(mode == MODE_PROMISCUOUS, "unsupported mode %d", mode);
  ZX_ASSERT_MSG(multicast_macs_count == 0, "unsupported multicast count %zu", multicast_macs_count);
}

static zx_driver_ops_t gvnic_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Gvnic::Bind;
  return ops;
}();

}  // namespace gvnic

ZIRCON_DRIVER(Gvnic, gvnic::gvnic_driver_ops, "zircon", GVNIC_VERSION);
