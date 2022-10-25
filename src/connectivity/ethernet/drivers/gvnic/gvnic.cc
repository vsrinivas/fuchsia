// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/gvnic/gvnic.h"

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

void Gvnic::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void Gvnic::DdkRelease() { delete this; }

static zx_driver_ops_t gvnic_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Gvnic::Bind;
  return ops;
}();

}  // namespace gvnic

ZIRCON_DRIVER(Gvnic, gvnic::gvnic_driver_ops, "zircon", GVNIC_VERSION);
