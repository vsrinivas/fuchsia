// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "igc_driver.h"

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>

#include "src/connectivity/ethernet/drivers/third_party/igc/igc_bind.h"

namespace ethernet {
namespace igc {

constexpr char kNetDevDriverName[] = "igc-netdev";

constexpr static size_t kEthTxDescBufTotalSize = kEthTxDescRingCount * kEthTxDescSize;
constexpr static size_t kEthRxDescBufTotalSize = kEthRxBufCount * kEthRxDescSize;

constexpr static bool kDoAutoNeg = true;
constexpr static uint8_t kAutoAllMode = 0;

constexpr static uint16_t kIffPromisc = 0x100;
constexpr static uint16_t kIffAllmulti = 0x200;
constexpr static uint16_t kMaxNumMulticastAddresses = 128;

constexpr static uint64_t kMaxIntsPerSec = 8000;
constexpr static uint64_t kDefaultItr = (1000000000 / (kMaxIntsPerSec * 256));
constexpr static uint32_t kIgcRadvVal = 64;

constexpr static uint8_t kIgcRxPthresh = 8;
constexpr static uint8_t kIgcRxHthresh = 8;
constexpr static uint8_t kIgcRxWthresh = 4;

constexpr static size_t kEtherAddrLen = 6;
constexpr static size_t kEtherMtu = 1500;
constexpr static uint8_t kPortId = 1;

IgcDriver::IgcDriver(zx_device_t* parent)
    : parent_(parent), netdev_impl_proto_{&this->network_device_impl_protocol_ops_, this} {
  zx_status_t status = ZX_OK;
  adapter_ = std::make_shared<struct adapter>();

  // Set up PCI.
  adapter_->osdep.pci = ddk::Pci(parent, "pci");
  auto& pci = adapter_->osdep.pci;

  if (!pci.is_valid()) {
    zxlogf(ERROR, "Failed to connect pci protocol.");
    return;
  }

  status = pci.SetBusMastering(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "cannot enable bus master %d", status);
    return;
  }

  zx::bti bti;
  status = pci.GetBti(0, &bti);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to get BTI");
    return;
  }
  adapter_->btih = bti.release();

  // Request 1 interrupt of any mode.
  status = pci.ConfigureInterruptMode(1, &adapter_->irq_mode);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to configure irqs");
    return;
  }

  zx::interrupt interrupt;
  status = pci.MapInterrupt(0, &interrupt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to map irq");
    return;
  }
  adapter_->irqh = interrupt.release();

  vmo_store::Options options = {
      .map =
          vmo_store::MapOptions{
              .vm_option = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
              .vmar = nullptr,
          },
      .pin =
          vmo_store::PinOptions{
              .bti = zx::unowned_bti(adapter_->btih),
              .bti_pin_options = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE,
              .index = true,
          },
  };

  // Initialize VmoStore.
  vmo_store_ = std::make_unique<VmoStore>(options);

  if (status = vmo_store_->Reserve(MAX_VMOS); status != ZX_OK) {
    zxlogf(ERROR, "failed to reserve the capacity of VmoStore to MAX_VMOS: %s",
           zx_status_get_string(status));
    return;
  }
}

IgcDriver::~IgcDriver() {
  igc_reset_hw(&adapter_->hw);
  adapter_->osdep.pci.SetBusMastering(false);
  io_buffer_release(&adapter_->desc_buffer);
  mmio_buffer_release(&adapter_->bar0_mmio);
  zx_handle_close(adapter_->btih);
  zx_handle_close(adapter_->irqh);
}

void IgcDriver::Release() { delete this; }

zx_status_t IgcDriver::Init() {
  zx_status_t status = ZX_OK;

  // Get and store hardware infomation.
  IdentifyHardware();
  AllocatePCIResources();

  struct igc_hw* hw = &adapter_->hw;

  s32 error = igc_setup_init_funcs(hw, true);
  if (error) {
    DEBUGOUT("Setup of Shared code failed, error %d\n", error);
    return ZX_ERR_INTERNAL;
  }

  igc_get_bus_info(hw);

  hw->mac.autoneg = kDoAutoNeg;
  hw->phy.autoneg_wait_to_complete = false;
  hw->phy.autoneg_advertised = ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF |
                               ADVERTISE_100_FULL | ADVERTISE_1000_FULL | ADVERTISE_2500_FULL;

  /* Copper options */
  if (hw->phy.media_type == igc_media_type_copper) {
    hw->phy.mdix = kAutoAllMode;
  }

  /* Check SOL/IDER usage */
  if (igc_check_reset_block(hw))
    DEBUGOUT(
        "PHY reset is blocked"
        " due to SOL/IDER session.\n");

  /*
  ** Start from a known state, this is
  ** important in reading the nvm and
  ** mac from that.
  */
  igc_reset_hw(hw);
  igc_power_up_phy(hw);

  /* Make sure we have a good EEPROM before we read from it */
  if (igc_validate_nvm_checksum(hw) < 0) {
    /*
    ** Some PCI-E parts fail the first check due to
    ** the link being in sleep state, call it again,
    ** if it fails a second time its a real issue.
    */
    if (igc_validate_nvm_checksum(hw) < 0) {
      DEBUGOUT("The EEPROM Checksum Is Not Valid\n");
      // TODO: Clean up states at these places.
      return ZX_ERR_INTERNAL;
    }
  }

  /* Copy the permanent MAC address out of the EEPROM */
  if (igc_read_mac_addr(hw) < 0) {
    DEBUGOUT(
        "EEPROM read error while reading MAC"
        " address\n");
    return ZX_ERR_INTERNAL;
  }

  if (!IsValidEthernetAddr(hw->mac.addr)) {
    DEBUGOUT("Invalid MAC address\n");
    return ZX_ERR_INTERNAL;
  }

  // Initialize the descriptor buffer, map and pin to contigous phy addesses.
  status = io_buffer_init(&adapter_->desc_buffer, adapter_->btih,
                          kEthTxDescBufTotalSize + kEthRxDescBufTotalSize,
                          IO_BUFFER_RW | IO_BUFFER_CONTIG);

  void* txrx_virt_addr = io_buffer_virt(&adapter_->desc_buffer);
  zx_paddr_t txrx_phy_addr = io_buffer_phys(&adapter_->desc_buffer);

  // Store the virtual and physical base addresses of Tx descriptor ring.
  adapter_->txdr = static_cast<struct igc_tx_desc*>(txrx_virt_addr);
  adapter_->txd_phys = txrx_phy_addr;

  // Store the virtual and physical base addresses of Rx descriptor ring. Following Tx addresses.
  adapter_->rxdr = reinterpret_cast<union igc_adv_rx_desc*>(adapter_->txdr + kEthTxDescRingCount);
  adapter_->rxd_phys = txrx_phy_addr + kEthTxDescBufTotalSize;

  InitTransmitUnit();
  InitReceiveUnit();

  u32 ctrl;
  ctrl = IGC_READ_REG(hw, IGC_CTRL);
  ctrl |= IGC_CTRL_VME;
  IGC_WRITE_REG(hw, IGC_CTRL, ctrl);

  // Promiscuous settings.
  IfSetPromisc(kIffPromisc);
  igc_clear_hw_cntrs_base_generic(hw);

  // Clear all pending interrupts.
  IGC_READ_REG(hw, IGC_ICR);
  IGC_WRITE_REG(hw, IGC_ICS, IGC_ICS_LSC);

  // The driver can now take control from firmware.
  uint32_t ctrl_ext = IGC_READ_REG(hw, IGC_CTRL_EXT);
  IGC_WRITE_REG(hw, IGC_CTRL_EXT, ctrl_ext | IGC_CTRL_EXT_DRV_LOAD);

  thrd_create_with_name(&adapter_->thread, IgcDriver::IgcIrqThreadFunc, this, "igc_irq_thread");
  thrd_detach(adapter_->thread);

  // Enable interrupts.
  u32 ims_mask = IMS_ENABLE_MASK;
  IGC_WRITE_REG(hw, IGC_IMS, ims_mask);

  OnlineStatusUpdate();

  // Add network device.
  static zx_protocol_device_t network_device_ops = {
      .version = DEVICE_OPS_VERSION,
      .release = [](void* ctx) { static_cast<IgcDriver*>(ctx)->Release(); },
  };

  device_add_args_t netDevArgs = {};
  netDevArgs.version = DEVICE_ADD_ARGS_VERSION;
  netDevArgs.name = kNetDevDriverName;
  netDevArgs.ctx = this;
  netDevArgs.ops = &network_device_ops;
  netDevArgs.proto_id = ZX_PROTOCOL_NETWORK_DEVICE_IMPL;
  netDevArgs.proto_ops = netdev_impl_proto_.ops;

  if ((status = device_add(parent_, &netDevArgs, &device_)) != ZX_OK) {
    zxlogf(ERROR, "Failed adding network device: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void IgcDriver::IdentifyHardware() {
  ddk::Pci& pci = adapter_->osdep.pci;
  struct igc_hw* hw = &adapter_->hw;

  // Make sure our PCI configuration space has the necessary stuff set.
  pci.ReadConfig16(PCI_CONFIG_COMMAND, &hw->bus.pci_cmd_word);

  fuchsia_hardware_pci::wire::DeviceInfo pci_info;
  zx_status_t status = pci.GetDeviceInfo(&pci_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pci_get_device_info failure");
    return;
  }

  hw->vendor_id = pci_info.vendor_id;
  hw->device_id = pci_info.device_id;
  hw->revision_id = pci_info.revision_id;
  pci.ReadConfig16(PCI_CONFIG_SUBSYSTEM_VENDOR_ID, &hw->subsystem_vendor_id);
  pci.ReadConfig16(PCI_CONFIG_SUBSYSTEM_ID, &hw->subsystem_device_id);

  // Do shared code init and setup.
  if (igc_set_mac_type(hw)) {
    zxlogf(ERROR, "igc_set_mac_type init failure");
    return;
  }
}

zx_status_t IgcDriver::AllocatePCIResources() {
  ddk::Pci& pci = adapter_->osdep.pci;

  // Map BAR0 memory
  zx_status_t status = pci.MapMmio(0u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &adapter_->bar0_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "PCI cannot map io %d", status);
    return status;
  }

  adapter_->osdep.membase = (uintptr_t)adapter_->bar0_mmio.vaddr;
  adapter_->hw.hw_addr = (u8*)&adapter_->osdep.membase;

  adapter_->hw.back = &adapter_->osdep;
  return ZX_OK;
}

void IgcDriver::InitTransmitUnit() {
  struct igc_hw* hw = &adapter_->hw;

  u32 tctl, txdctl = 0;
  u64 bus_addr = adapter_->txd_phys;
  adapter_->txh_ind = 0;
  adapter_->txt_ind = 0;

  // Base and length of TX Ring.
  IGC_WRITE_REG(hw, IGC_TDLEN(0), kEthTxDescRingCount * sizeof(struct igc_tx_desc));
  IGC_WRITE_REG(hw, IGC_TDBAH(0), (u32)(bus_addr >> 32));
  IGC_WRITE_REG(hw, IGC_TDBAL(0), (u32)bus_addr);
  // Init the HEAD/TAIL indices.
  IGC_WRITE_REG(hw, IGC_TDT(0), 0);
  IGC_WRITE_REG(hw, IGC_TDH(0), 0);

  DEBUGOUT("Base = %x, Length = %x\n", IGC_READ_REG(hw, IGC_TDBAL(0)),
           IGC_READ_REG(hw, IGC_TDLEN(0)));

  txdctl = 0;        /* clear txdctl */
  txdctl |= 0x1f;    /* PTHRESH */
  txdctl |= 1 << 8;  /* HTHRESH */
  txdctl |= 1 << 16; /* WTHRESH */
  txdctl |= 1 << 22; /* Reserved bit 22 must always be 1 */
  txdctl |= IGC_TXDCTL_GRAN;
  txdctl |= 1 << 25; /* LWTHRESH */

  IGC_WRITE_REG(hw, IGC_TXDCTL(0), txdctl);

  IGC_WRITE_REG(hw, IGC_TIDV, 1);
  IGC_WRITE_REG(hw, IGC_TADV, 64);

  // Program the Transmit Control Register.
  tctl = IGC_READ_REG(hw, IGC_TCTL);
  tctl &= ~IGC_TCTL_CT;
  tctl |= (IGC_TCTL_PSP | IGC_TCTL_RTLC | IGC_TCTL_EN | (IGC_COLLISION_THRESHOLD << IGC_CT_SHIFT));
  tctl |= IGC_TCTL_MULR;

  // This write will effectively turn on the transmit unit.
  IGC_WRITE_REG(hw, IGC_TCTL, tctl);
}

void IgcDriver::InitReceiveUnit() {
  struct igc_hw* hw = &adapter_->hw;
  u32 rctl = 0, rxcsum = 0, srrctl = 0, rxdctl = 0;

  // Make sure receives are disabled while setting up the descriptor ring.
  rctl = IGC_READ_REG(hw, IGC_RCTL);
  IGC_WRITE_REG(hw, IGC_RCTL, rctl & ~IGC_RCTL_EN);

  // Setup the Receive Control Register.
  rctl &= ~(3 << IGC_RCTL_MO_SHIFT);
  rctl |= IGC_RCTL_EN | IGC_RCTL_BAM | IGC_RCTL_LBM_NO | IGC_RCTL_RDMTS_HALF |
          (hw->mac.mc_filter_type << IGC_RCTL_MO_SHIFT);

  // Do not store bad packets.
  rctl &= ~IGC_RCTL_SBP;

  // Disable Long Packet receive.
  rctl &= ~IGC_RCTL_LPE;

  // Strip the CRC.
  rctl |= IGC_RCTL_SECRC;

  // Set the interrupt throttling rate. Value is calculated as DEFAULT_ITR = 1/(MAX_INTS_PER_SEC *
  // 256ns).

  IGC_WRITE_REG(hw, IGC_RADV, kIgcRadvVal);
  IGC_WRITE_REG(hw, IGC_ITR, kDefaultItr);
  IGC_WRITE_REG(hw, IGC_RDTR, 0);

  rxcsum = IGC_READ_REG(hw, IGC_RXCSUM);
  rxcsum &= ~IGC_RXCSUM_TUOFL;
  IGC_WRITE_REG(hw, IGC_RXCSUM, rxcsum);

  // Assume packet size won't be larger than MTU.
  srrctl |= 2048 >> IGC_SRRCTL_BSIZEPKT_SHIFT;
  rctl |= IGC_RCTL_SZ_2048;

  u64 bus_addr = adapter_->rxd_phys;
  adapter_->rxh_ind = 0;
  adapter_->rxt_ind = 0;

  srrctl |= IGC_SRRCTL_DESCTYPE_ADV_ONEBUF;

  IGC_WRITE_REG(hw, IGC_RDLEN(0), kEthRxBufCount * sizeof(union igc_adv_rx_desc));
  IGC_WRITE_REG(hw, IGC_RDBAH(0), (uint32_t)(bus_addr >> 32));
  IGC_WRITE_REG(hw, IGC_RDBAL(0), (uint32_t)bus_addr);
  IGC_WRITE_REG(hw, IGC_SRRCTL(0), srrctl);
  // Setup the HEAD/TAIL descriptor pointers.
  IGC_WRITE_REG(hw, IGC_RDH(0), 0);
  IGC_WRITE_REG(hw, IGC_RDT(0), 0);
  // Enable this Queue.
  rxdctl = IGC_READ_REG(hw, IGC_RXDCTL(0));
  rxdctl |= IGC_RXDCTL_QUEUE_ENABLE;
  rxdctl &= 0xFFF00000;
  rxdctl |= kIgcRxPthresh;
  rxdctl |= kIgcRxHthresh << 8;
  rxdctl |= kIgcRxWthresh << 16;
  IGC_WRITE_REG(hw, IGC_RXDCTL(0), rxdctl);

  do {
    rxdctl = IGC_READ_REG(hw, IGC_RXDCTL(0));
  } while (!(rxdctl & IGC_RXDCTL_QUEUE_ENABLE));

  // Make sure VLAN filters are off.
  rctl &= ~IGC_RCTL_VFE;

  // Write out the settings.
  IGC_WRITE_REG(hw, IGC_RCTL, rctl);
}

void IgcDriver::IfSetPromisc(uint32_t flags) {
  u32 reg_rctl;
  int mcnt = 0;
  struct igc_hw* hw = &adapter_->hw;

  reg_rctl = IGC_READ_REG(hw, IGC_RCTL);
  reg_rctl &= ~(IGC_RCTL_SBP | IGC_RCTL_UPE);
  if (flags & kIffAllmulti)
    mcnt = kMaxNumMulticastAddresses;

  // Don't disable if in MAX groups.
  if (mcnt < kMaxNumMulticastAddresses)
    reg_rctl &= (~IGC_RCTL_MPE);
  IGC_WRITE_REG(hw, IGC_RCTL, reg_rctl);

  if (flags & kIffPromisc) {
    reg_rctl |= (IGC_RCTL_UPE | IGC_RCTL_MPE);
    // Turn this on if you want to see bad packets.
    reg_rctl |= IGC_RCTL_SBP;
    IGC_WRITE_REG(hw, IGC_RCTL, reg_rctl);
  } else if (flags & kIffAllmulti) {
    reg_rctl |= IGC_RCTL_MPE;
    reg_rctl &= ~IGC_RCTL_UPE;
    IGC_WRITE_REG(hw, IGC_RCTL, reg_rctl);
  }
}

bool IgcDriver::IsValidEthernetAddr(uint8_t* addr) {
  const char zero_addr[6] = {0, 0, 0, 0, 0, 0};

  if ((addr[0] & 1) || (!bcmp(addr, zero_addr, kEtherAddrLen))) {
    return false;
  }

  return true;
}

void IgcDriver::ReapTxBuffers() {
  std::lock_guard lock(adapter_->tx_lock);
  // Allocate the result array with max tx buffer pool size which is 256.
  tx_result_t results[kEthTxBufCount];
  uint32_t& n = adapter_->txh_ind;
  size_t reap_count = 0;

  while (adapter_->txdr[n].upper.fields.status & IGC_TXD_STAT_DD) {
    adapter_->txdr[n].upper.fields.status = 0;
    results[reap_count].id = tx_buffers_[n].buffer_id;
    // We don't have a way to get the tx status of this packet from the tx descriptor since there
    // is no other STAT macros defined in FreeBSD driver,
    // so always return ZX_OK here.
    // TODO(fxbug.dev/111725): Optimize it when we get the handbook.
    results[reap_count].status = ZX_OK;

    reap_count++;
    n = (n + 1) & (kEthTxDescRingCount - 1);
  }
  adapter_->netdev_ifc.CompleteTx(results, reap_count);
}

bool IgcDriver::OnlineStatusUpdate() {
  std::lock_guard lock(online_mutex_);
  bool new_status = IGC_READ_REG(&adapter_->hw, IGC_STATUS) & IGC_STATUS_LU;
  if (new_status == online_) {
    return false;
  }
  online_ = new_status;
  return true;
}

// NetworkDevice::Callbacks implementations
zx_status_t IgcDriver::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  adapter_->netdev_ifc = ::ddk::NetworkDeviceIfcProtocolClient(iface);
  adapter_->netdev_ifc.AddPort(kPortId, this, &network_port_protocol_ops_);
  return ZX_OK;
}

void IgcDriver::NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie) {
  std::lock_guard lock(started_mutex_);
  started_ = true;
  callback(cookie, ZX_OK);
}

void IgcDriver::NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie) {
  std::lock_guard lock(started_mutex_);
  started_ = false;
  {
    // Reclaim all the buffers queued in rx descriptor ring.
    std::lock_guard lock(adapter_->rx_lock);
    rx_buffer_t buffers[kEthRxBufCount];
    rx_buffer_part_t buffer_parts[std::size(buffers)];
    size_t buf_idx = 0;

    uint32_t& rxh = adapter_->rxh_ind;
    const uint32_t& rxt = adapter_->rxh_ind;

    // The last buffer on rxt will also be reclaimed under this loop condition.
    while (rxh != ((rxt + 1) & (kEthRxBufCount - 1))) {
      // Populate empty buffers.
      buffers[buf_idx].data_count = 0;
      buffers[buf_idx].meta.port = kPortId;
      buffers[buf_idx].data_list = &buffer_parts[buf_idx];
      buffers[buf_idx].meta.frame_type =
          static_cast<uint8_t>(::fuchsia_hardware_network::wire::FrameType::kEthernet);
      buffer_parts[buf_idx].id = rx_buffers_[rxh].buffer_id;
      buffer_parts[buf_idx].length = 0;
      buffer_parts[buf_idx].offset = 0;

      rx_buffers_[rxh].available = false;
      // Clean up the status flag in rx descriptor.
      adapter_->rxdr[rxh].wb.upper.status_error = 0;
      rxh = (rxh + 1) & (kEthRxBufCount - 1);
      buf_idx++;
      ZX_ASSERT_MSG(buf_idx <= kEthRxBufCount, "buf_idx: %zu", buf_idx);
    }

    // Now rxh is one step ahead than rxt, resume rxh to the rxt.
    rxh = rxt;
    adapter_->netdev_ifc.CompleteRx(buffers, buf_idx);
  }

  // Reclaim all tx buffers. Tx data path is protected by start lock.
  size_t res_idx = 0;
  tx_result_t results[kEthTxBufCount];
  uint32_t& txh = adapter_->txh_ind;
  const uint32_t& txt = adapter_->txt_ind;
  while (txh != txt) {
    adapter_->txdr[txh].upper.fields.status = 0;
    results[res_idx].id = tx_buffers_[txh].buffer_id;
    results[res_idx].status = ZX_ERR_UNAVAILABLE;
    res_idx++;
    txh = (txh + 1) & (kEthTxDescRingCount - 1);
  }
  adapter_->netdev_ifc.CompleteTx(results, res_idx);

  callback(ZX_OK);
}

void IgcDriver::NetworkDeviceImplGetInfo(device_info_t* out_info) {
  memset(out_info, 0, sizeof(*out_info));
  out_info->tx_depth = kEthTxBufCount;
  out_info->rx_depth = kEthRxBufCount;
  out_info->rx_threshold = out_info->rx_depth / 2;
  out_info->max_buffer_parts = 1;
  out_info->max_buffer_length = ZX_PAGE_SIZE / 2;
  out_info->buffer_alignment = ZX_PAGE_SIZE / 2;
  out_info->min_rx_buffer_length = 2048;
  out_info->min_tx_buffer_length = 60;

  out_info->tx_head_length = 0;
  out_info->tx_tail_length = 0;
  // No hardware acceleration supported yet.
  out_info->rx_accel_count = 0;
  out_info->tx_accel_count = 0;
}

void IgcDriver::NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list, size_t buffers_count) {
  network::SharedAutoLock autp_lock(&vmo_lock_);
  std::lock_guard tx_lock(adapter_->tx_lock);
  std::lock_guard start_lock(started_mutex_);
  if (!started_) {
    std::vector<tx_result_t> results(buffers_count);
    for (size_t i = 0; i < buffers_count; ++i) {
      results[i].id = buffers_list[i].id;
      results[i].status = ZX_ERR_UNAVAILABLE;
    }
    adapter_->netdev_ifc.CompleteTx(results.data(), results.size());
    return;
  }

  uint32_t& n = adapter_->txt_ind;
  for (size_t i = 0; i < buffers_count; i++) {
    const tx_buffer_t& buffer = buffers_list[i];
    ZX_ASSERT_MSG(buffer.data_count == 1,
                  "Tx buffer contains multiple regions, which does not fit the configuration.");
    const buffer_region_t& region = buffer.data_list[0];
    // Get physical address of buffers from region data.
    VmoStore::StoredVmo* stored_vmo = vmo_store_->GetVmo(region.vmo);
    ZX_ASSERT_MSG(stored_vmo != nullptr, "invalid VMO id %d", region.vmo);

    fzl::PinnedVmo::Region phy_region;
    size_t actual_regions = 0;
    zx_status_t status =
        stored_vmo->GetPinnedRegions(region.offset, region.length, &phy_region, 1, &actual_regions);
    ZX_ASSERT_MSG(status == ZX_OK, "failed to retrieve pinned region %s (actual=%zu)",
                  zx_status_get_string(status), actual_regions);

    // Modify tx descriptors and store buffer id.
    igc_tx_desc* cur_desc = &adapter_->txdr[n];
    tx_buffers_[n].buffer_id = buffer.id;
    cur_desc->buffer_addr = phy_region.phys_addr;
    cur_desc->lower.data =
        (uint32_t)(IGC_TXD_CMD_EOP | IGC_TXD_CMD_IFCS | IGC_TXD_CMD_RS | region.length);

    // Don't update tx descriptor ring tail index if it's the last buffer of this set, make sure the
    // tail pointer points to a filled buffer.
    n = (n + 1) & (kEthTxDescRingCount - 1);
  }

  //  Update the last filled buffer to the adapter.
  IGC_WRITE_REG(&adapter_->hw, IGC_TDT(0), n);
}

void IgcDriver::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list,
                                              size_t buffers_count) {
  network::SharedAutoLock autp_lock(&vmo_lock_);
  std::lock_guard lock(adapter_->rx_lock);

  // Get current rx descriptor ring tail index.
  uint32_t& rxdr_tail = adapter_->rxt_ind;
  for (size_t i = 0; i < buffers_count; i++) {
    const rx_space_buffer_t& buffer = buffers_list[i];
    const buffer_region_t& vmo_region = buffer.region;

    // Store the buffer id info into internal buffer info array. It will be used by the interrupt
    // thread for reporting received buffers.
    rx_buffers_[rxdr_tail].buffer_id = buffer.id;
    // If this check fails, it means that the rx descriptor head/tail indexes are out of sync.
    ZX_ASSERT_MSG(rx_buffers_[rxdr_tail].available == false,
                  "All buffers are assigned as rx space.");
    rx_buffers_[rxdr_tail].available = true;

    VmoStore::StoredVmo* stored_vmo = vmo_store_->GetVmo(vmo_region.vmo);
    ZX_ASSERT_MSG(stored_vmo != nullptr, "invalid VMO id %d", vmo_region.vmo);

    fzl::PinnedVmo::Region phy_region;
    size_t actual_regions = 0;
    zx_status_t status = stored_vmo->GetPinnedRegions(vmo_region.offset, vmo_region.length,
                                                      &phy_region, 1, &actual_regions);
    ZX_ASSERT_MSG(status == ZX_OK, "failed to retrieve pinned region %s (actual=%zu)",
                  zx_status_get_string(status), actual_regions);

    // Make rx descriptor ready.
    union igc_adv_rx_desc* cur_desc = &adapter_->rxdr[rxdr_tail];
    cur_desc->read.pkt_addr = phy_region.phys_addr;
    ZX_ASSERT_MSG(phy_region.size >= kEthRxBufSize,
                  "Something went wrong physical buffer allocation.");
    cur_desc->wb.upper.status_error = 0;

    // Increase the tail index of rx descriptor ring, note that after this loop ends, rxdr_tail
    // should points to the desciptor right after the last ready-to-write buffer's descriptor.
    rxdr_tail = (rxdr_tail + 1) & (kEthRxBufCount - 1);
  }

  // Inform the hw the last ready-to-write buffer(which is at rxdr_tail - 1) by setting the
  // descriptor tail pointer.
  IGC_WRITE_REG(&adapter_->hw, IGC_RDT(0), rxdr_tail - 1);
}

void IgcDriver::NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo,
                                            network_device_impl_prepare_vmo_callback callback,
                                            void* cookie) {
  size_t size;
  vmo.get_size(&size);
  fbl::AutoLock vmo_lock(&vmo_lock_);

  zx_status_t status = vmo_store_->RegisterWithKey(id, std::move(vmo));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to store VMO(with mapping and pinning): %s",
           zx_status_get_string(status));
    callback(cookie, status);
    return;
  }
  callback(cookie, ZX_OK);
}

void IgcDriver::NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {
  fbl::AutoLock vmo_lock(&vmo_lock_);
  zx::status<zx::vmo> status = vmo_store_->Unregister(vmo_id);
  if (status.status_value() != ZX_OK) {
    zxlogf(ERROR, "Failed to release VMO: %s", status.status_string());
  }
}

void IgcDriver::NetworkDeviceImplSetSnoop(bool snoop) {}

constexpr uint8_t kRxTypesList[] = {
    static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet)};
constexpr tx_support_t kTxTypesList[] = {{
    .type = static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
    .features = fuchsia_hardware_network::wire::kFrameFeaturesRaw,
    .supported_flags = 0,
}};

// NetworkPort protocol implementations.
void IgcDriver::NetworkPortGetInfo(port_info_t* out_info) {
  *out_info = {
      .port_class = static_cast<uint32_t>(fuchsia_hardware_network::wire::DeviceClass::kEthernet),
      .rx_types_list = kRxTypesList,
      .rx_types_count = std::size(kRxTypesList),
      .tx_types_list = kTxTypesList,
      .tx_types_count = std::size(kTxTypesList),
  };
}

void IgcDriver::NetworkPortGetStatus(port_status_t* out_status) {
  OnlineStatusUpdate();
  *out_status = {
      .mtu = kEtherMtu,
      .flags =
          online_ ? static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline) : 0,
  };
}

void IgcDriver::NetworkPortSetActive(bool active) { /* Do nothing here.*/
}

void IgcDriver::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) {
  *out_mac_ifc = {
      .ops = &mac_addr_protocol_ops_,
      .ctx = this,
  };
}

void IgcDriver::NetworkPortRemoved() { /* Do nothing here, we don't remove port in this driver.*/
}

void IgcDriver::MacAddrGetAddress(uint8_t* out_mac) {
  memcpy(out_mac, adapter_->hw.mac.addr, kEtherAddrLen);
}

void IgcDriver::MacAddrGetFeatures(features_t* out_features) {
  *out_features = {
      .multicast_filter_count = 0,
      .supported_modes = MODE_PROMISCUOUS,
  };
}

void IgcDriver::MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                               size_t multicast_macs_count) {
  /* Do nothing here.*/
}

int IgcDriver::IgcIrqThreadFunc(void* arg) {
  auto drv = static_cast<IgcDriver*>(arg);
  std::shared_ptr<ethernet::igc::IgcDriver::adapter> adapter = drv->Adapter();
  struct igc_hw* hw = &adapter->hw;

  // Allocate a buffer array at half of the rx buffer ring size, and this limits the maximum number
  // of packets that the driver passes up in one batch.
  constexpr size_t kRxBuffersPerBatch = kEthRxBufCount / 2;
  rx_buffer_t buffers[kRxBuffersPerBatch];
  rx_buffer_part_t buffer_parts[std::size(buffers)];

  // Clean up all the buffers and buffer_parts.
  memset(buffers, 0, kRxBuffersPerBatch * sizeof(buffers[0]));
  memset(buffer_parts, 0, kRxBuffersPerBatch * sizeof(buffer_parts[0]));

  IgcDriver::buffer_info* rx_buffer_info = drv->RxBuffer();

  for (;;) {
    zx_status_t r;
    r = zx_interrupt_wait(adapter->irqh, NULL);
    if (r != ZX_OK) {
      DEBUGOUT("irq wait failed? %d", r);
      break;
    }

    unsigned irq = 0;
    irq = IGC_READ_REG(hw, IGC_ICR);
    if (irq & IGC_ICR_RXT0) {
      {
        std::lock_guard lock(drv->started_mutex_);
        if (drv->started_ == false) {
          // skip the excessive rx interrupts if driver is stopped. The rx descriptor ring has been
          // cleaned up.
          continue;
        }
      }

      {
        std::lock_guard lock(adapter->rx_lock);
        // The index for accessing "buffers" on a per interrupt basis.
        size_t buf_idx = 0;
        while ((adapter->rxdr[adapter->rxh_ind].wb.upper.status_error & IGC_RXD_STAT_DD)) {
          // Call complete_rx to pass the packet up to network device.
          buffers[buf_idx].data_count = 1;
          buffers[buf_idx].meta.port = kPortId;
          buffers[buf_idx].data_list = &buffer_parts[buf_idx];
          buffers[buf_idx].meta.frame_type =
              static_cast<uint8_t>(::fuchsia_hardware_network::wire::FrameType::kEthernet);
          buffer_parts[buf_idx].id = rx_buffer_info[adapter->rxh_ind].buffer_id;
          buffer_parts[buf_idx].length = adapter->rxdr[adapter->rxh_ind].wb.upper.length;
          buffer_parts[buf_idx].offset = 0;

          // Release the buffer in internal buffer info array.
          rx_buffer_info[adapter->rxh_ind].available = false;

          // Update the head index of rx descriptor ring.
          adapter->rxh_ind = (adapter->rxh_ind + 1) & (kEthRxBufCount - 1);

          // Update buffer index and check whether the local buffer array is full.
          buf_idx++;
          if (buf_idx == kRxBuffersPerBatch) {  // Local buffer array is full.
            // Pass up a full batch of packets and reset buffer index.
            adapter->netdev_ifc.CompleteRx(buffers, kRxBuffersPerBatch);
            buf_idx = 0;

            // Clean up used buffers.
            memset(buffers, 0, kRxBuffersPerBatch * sizeof(buffers[0]));
            memset(buffer_parts, 0, kRxBuffersPerBatch * sizeof(buffer_parts[0]));
          }
        }
        if (buf_idx != 0) {
          adapter->netdev_ifc.CompleteRx(buffers, buf_idx);
          // Clean up used buffers.
          memset(buffers, 0, buf_idx * sizeof(buffers[0]));
          memset(buffer_parts, 0, buf_idx * sizeof(buffer_parts[0]));
        }
      }
    }
    if (irq & IGC_ICR_TXDW) {
      // Reap the tx buffers as much as possible when there is a tx write back interrupt.
      drv->ReapTxBuffers();
    }
    if (irq & IGC_ICR_LSC) {
      if (drv->OnlineStatusUpdate()) {
        port_status_t status = {
            .mtu = kEtherMtu,
            .flags = drv->online_ ? static_cast<uint32_t>(
                                        fuchsia_hardware_network::wire::StatusFlags::kOnline)
                                  : 0,
        };
        adapter->netdev_ifc.PortStatusChanged(kPortId, &status);
      }
    }
    if (adapter->irq_mode == fuchsia_hardware_pci::InterruptMode::kLegacy) {
      adapter->osdep.pci.AckInterrupt();
    }
  }
  return 0;
}

}  // namespace igc
}  // namespace ethernet

static zx_status_t igc_bind(void* ctx, zx_device_t* device) {
  std::printf("%s\n", __func__);
  zx_status_t status = ZX_OK;

  auto igc = std::make_unique<ethernet::igc::IgcDriver>(device);
  if ((status = igc->Init()) != ZX_OK) {
    return status;
  }

  // devhost is now responsible for the memory allocated for object igc.
  igc.release();
  return ZX_OK;
}

static zx_driver_ops_t igc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = igc_bind,
};

ZIRCON_DRIVER(igc, igc_driver_ops, "zircon", "0.1");
