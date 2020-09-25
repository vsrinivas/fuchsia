/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Nicole Graziano <nicole@nextbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "ddk/device.h"
#include "ddk/driver.h"
#include "e1000_api.h"

typedef enum {
  ETH_RUNNING = 0,
  ETH_SUSPENDING,
  ETH_SUSPENDED,
} eth_state;

#define IFF_PROMISC 0x100
#define IFF_ALLMULTI 0x200

#define em_mac_min e1000_82547
#define igb_mac_min e1000_82575

#define EM_RADV 64
#define EM_RDTR 0

#define IGB_RX_PTHRESH \
  ((hw->mac.type == e1000_i354) ? 12 : ((hw->mac.type <= e1000_82576) ? 16 : 8))
#define IGB_RX_HTHRESH 8
#define IGB_RX_WTHRESH ((hw->mac.type == e1000_82576) ? 1 : 4)

#define IGB_TX_PTHRESH ((hw->mac.type == e1000_i354) ? 20 : 8)
#define IGB_TX_HTHRESH 1
#define IGB_TX_WTHRESH ((hw->mac.type != e1000_82575) ? 1 : 16)

#define MAX_INTS_PER_SEC 8000
#define DEFAULT_ITR (1000000000 / (MAX_INTS_PER_SEC * 256))

/* PCI Config defines */
#define EM_BAR_TYPE(v) ((v)&EM_BAR_TYPE_MASK)
#define EM_BAR_TYPE_MASK 0x00000001
#define EM_BAR_TYPE_MMEM 0x00000000
#define EM_BAR_TYPE_IO 0x00000001
#define EM_BAR_TYPE_FLASH 0x0014
#define EM_BAR_MEM_TYPE(v) ((v)&EM_BAR_MEM_TYPE_MASK)
#define EM_BAR_MEM_TYPE_MASK 0x00000006
#define EM_BAR_MEM_TYPE_32BIT 0x00000000
#define EM_BAR_MEM_TYPE_64BIT 0x00000004
#define EM_MSIX_BAR 3 /* On 82575 */

#define ETH_MTU 1500

/* tunables */
#define ETH_RXBUF_SIZE 2048
#define ETH_RXHDR_SIZE 256
#define ETH_RXBUF_COUNT 32

#define ETH_TXBUF_SIZE 2048
#define ETH_TXBUF_COUNT 32
#define ETH_TXBUF_HSIZE 128
#define ETH_TXBUF_DSIZE (ETH_TXBUF_SIZE - ETH_TXBUF_HSIZE)

#define ETH_DRING_SIZE 2048

#define ETH_ALLOC                                                            \
  ((ETH_RXBUF_SIZE * ETH_RXBUF_COUNT) + (ETH_RXHDR_SIZE * ETH_RXBUF_COUNT) + \
   (ETH_TXBUF_SIZE * ETH_TXBUF_COUNT) + (ETH_DRING_SIZE * 2))

struct framebuf {
  list_node_t node;
  uintptr_t phys;
  void* data;
  size_t size;
};

/*
 * See Intel 82574 Driver Programming Interface Manual, Section 10.2.6.9
 */
#define TARC_SPEED_MODE_BIT (1 << 21) /* On PCI-E MACs only */
#define TARC_ERRATA_BIT (1 << 26) /* Note from errata on 82574 */

struct txrx_funcs;

struct adapter {
  struct e1000_hw hw;
  struct e1000_osdep osdep;
  mtx_t lock;
  zx_device_t* zxdev;
  thrd_t thread;
  zx_handle_t irqh;
  zx_handle_t btih;
  io_buffer_t buffer;
  list_node_t free_frames;
  list_node_t busy_frames;

  // tx/rx descriptor rings
  struct e1000_tx_desc* txd;
  struct e1000_rx_desc* rxd;

  // base physical addresses for
  // tx/rx rings and rx buffers
  // store as 64bit integer to match hw register size
  uint64_t txd_phys;
  uint64_t rxd_phys;
  uint64_t rxb_phys;
  uint64_t rxh_phys;
  void* rxb;
  void* rxh;
  bool online;

  eth_state state;

  // callback interface to attached ethernet layer
  ethernet_ifc_protocol_t ifc;

  uint32_t tx_wr_ptr;
  uint32_t tx_rd_ptr;
  uint32_t rx_rd_ptr;

  mtx_t send_lock;

  mmio_buffer_t bar0_mmio;
  mmio_buffer_t flash_mmio;
  struct txrx_funcs* txrx;
};

static inline void eth_enable_rx(struct adapter* adapter) {
  uint32_t rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);
  E1000_WRITE_REG(&adapter->hw, E1000_RCTL, rctl | E1000_RCTL_EN);
}

static inline void eth_disable_rx(struct adapter* adapter) {
  uint32_t rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);
  E1000_WRITE_REG(&adapter->hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);
}

static inline void eth_enable_tx(struct adapter* adapter) {
  uint32_t tctl = E1000_READ_REG(&adapter->hw, E1000_TCTL);
  E1000_WRITE_REG(&adapter->hw, E1000_TCTL, tctl | E1000_TCTL_EN);
}

static inline void eth_disable_tx(struct adapter* adapter) {
  uint32_t tctl = E1000_READ_REG(&adapter->hw, E1000_TCTL);
  E1000_WRITE_REG(&adapter->hw, E1000_TCTL, tctl & ~E1000_TCTL_EN);
}

static void reap_tx_buffers(struct adapter* adapter) {
  uint32_t n = adapter->tx_rd_ptr;
  for (;;) {
    struct e1000_tx_desc* desc = &adapter->txd[n];
    if (!(desc->upper.fields.status & E1000_TXD_STAT_DD)) {
      break;
    }
    struct framebuf* frame = list_remove_head_type(&adapter->busy_frames, struct framebuf, node);
    if (frame == NULL) {
      ZX_PANIC("e1000: frame is invalid");
    }
    // TODO: verify that this is the matching buffer to txd[n] addr?
    list_add_tail(&adapter->free_frames, &frame->node);
    desc->upper.fields.status = 0;
    n = (n + 1) & (ETH_TXBUF_COUNT - 1);
  }
  adapter->tx_rd_ptr = n;
}

static size_t eth_tx_queued(struct adapter* adapter) {
  reap_tx_buffers(adapter);
  return ((adapter->tx_wr_ptr + ETH_TXBUF_COUNT) - adapter->tx_rd_ptr) & (ETH_TXBUF_COUNT - 1);
}

static void e1000_suspend(void* ctx, uint8_t requested_state, bool enable_wake,
                          uint8_t suspend_reason) {
  DEBUGOUT("entry\n");
  struct adapter* adapter = ctx;
  mtx_lock(&adapter->lock);
  adapter->state = ETH_SUSPENDING;

  // Immediately disable the rx queue
  eth_disable_rx(adapter);

  // Wait for queued tx packets to complete
  int iterations = 0;
  do {
    if (!eth_tx_queued(adapter)) {
      goto tx_done;
    }
    mtx_unlock(&adapter->lock);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    iterations++;
    mtx_lock(&adapter->lock);
  } while (iterations < 10);
  DEBUGOUT("timed out waiting for tx queue to drain when suspending\n");

tx_done:
  eth_disable_tx(adapter);
  e1000_power_down_phy(&adapter->hw);
  adapter->state = ETH_SUSPENDED;
  device_suspend_reply(adapter->zxdev, ZX_OK, requested_state);
  mtx_unlock(&adapter->lock);
}

static void e1000_resume(void* ctx, uint32_t requested_perf_state) {
  DEBUGOUT("entry\n");
  struct adapter* adapter = ctx;
  mtx_lock(&adapter->lock);
  e1000_power_up_phy(&adapter->hw);
  eth_enable_rx(adapter);
  eth_enable_tx(adapter);
  adapter->state = ETH_RUNNING;
  device_resume_reply(adapter->zxdev, ZX_OK, DEV_POWER_STATE_D0, requested_perf_state);
  mtx_unlock(&adapter->lock);
}

static void e1000_release(void* ctx) {
  DEBUGOUT("entry\n");
  struct adapter* adapter = ctx;
  e1000_reset_hw(&adapter->hw);
  pci_enable_bus_master(&adapter->osdep.pci, false);

  io_buffer_release(&adapter->buffer);
  mmio_buffer_release(&adapter->bar0_mmio);
  mmio_buffer_release(&adapter->flash_mmio);

  zx_handle_close(adapter->btih);
  zx_handle_close(adapter->irqh);
  free(adapter);
}

static zx_protocol_device_t e1000_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .suspend = e1000_suspend,
    .resume = e1000_resume,
    .release = e1000_release,
};

struct txrx_funcs {
  zx_status_t (*eth_rx)(struct adapter* adapter, void** data, size_t* len);
  void (*eth_rx_ack)(struct adapter* adapter);
  void (*rxd_setup)(struct adapter* adapter);
};

zx_status_t igb_eth_rx(struct adapter* adapter, void** data, size_t* len) {
  uint32_t n = adapter->rx_rd_ptr;
  union e1000_adv_rx_desc* desc = (union e1000_adv_rx_desc*)&adapter->rxd[n];

  if (!(desc->wb.upper.status_error & E1000_RXD_STAT_DD)) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // copy out packet
  *data = adapter->rxb + ETH_RXBUF_SIZE * n;
  *len = desc->wb.upper.length;

  return ZX_OK;
}

void igb_eth_rx_ack(struct adapter* adapter) {
  uint32_t n = adapter->rx_rd_ptr;
  union e1000_adv_rx_desc* desc = (union e1000_adv_rx_desc*)&adapter->rxd[n];

  // make buffer available to hw
  desc->read.pkt_addr = adapter->rxb_phys + ETH_RXBUF_SIZE * n;
  desc->read.hdr_addr = adapter->rxh_phys + ETH_RXHDR_SIZE * n;
}

void igb_rxd_setup(struct adapter* adapter) {
  union e1000_adv_rx_desc* rxd = (union e1000_adv_rx_desc*)adapter->rxd;

  for (int n = 0; n < ETH_RXBUF_COUNT; n++) {
    rxd[n].read.pkt_addr = adapter->rxb_phys + ETH_RXBUF_SIZE * n;
    rxd[n].read.hdr_addr = adapter->rxh_phys + ETH_RXHDR_SIZE * n;
  }
}

struct txrx_funcs igb_txrx = {
    .eth_rx = igb_eth_rx, .eth_rx_ack = igb_eth_rx_ack, .rxd_setup = igb_rxd_setup};

zx_status_t em_eth_rx(struct adapter* adapter, void** data, size_t* len) {
  uint32_t n = adapter->rx_rd_ptr;
  union e1000_rx_desc_extended* desc = (union e1000_rx_desc_extended*)&adapter->rxd[n];

  if (!(desc->wb.upper.status_error & E1000_RXD_STAT_DD)) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // copy out packet
  *data = adapter->rxb + ETH_RXBUF_SIZE * n;
  *len = desc->wb.upper.length;

  return ZX_OK;
}

void em_eth_rx_ack(struct adapter* adapter) {
  uint32_t n = adapter->rx_rd_ptr;
  union e1000_rx_desc_extended* desc = (union e1000_rx_desc_extended*)&adapter->rxd[n];

  /* Zero out the receive descriptors status. */
  desc->read.buffer_addr = adapter->rxb_phys + ETH_RXBUF_SIZE * n;
  desc->wb.upper.status_error = 0;
}

void em_rxd_setup(struct adapter* adapter) {
  union e1000_rx_desc_extended* rxd = (union e1000_rx_desc_extended*)adapter->rxd;

  for (int n = 0; n < ETH_RXBUF_COUNT; n++) {
    rxd[n].read.buffer_addr = adapter->rxb_phys + ETH_RXBUF_SIZE * n;
    /* DD bits must be cleared */
    rxd[n].wb.upper.status_error = 0;
  }
}

struct txrx_funcs em_txrx = {
    .eth_rx = em_eth_rx, .eth_rx_ack = em_eth_rx_ack, .rxd_setup = em_rxd_setup};

zx_status_t lem_eth_rx(struct adapter* adapter, void** data, size_t* len) {
  uint32_t n = adapter->rx_rd_ptr;
  struct e1000_rx_desc* desc = &adapter->rxd[n];

  if (!(desc->status & E1000_RXD_STAT_DD)) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // copy out packet
  *data = adapter->rxb + ETH_RXBUF_SIZE * n;
  *len = desc->length;

  return ZX_OK;
}

void lem_eth_rx_ack(struct adapter* adapter) {
  uint32_t n = adapter->rx_rd_ptr;
  struct e1000_rx_desc* desc = &adapter->rxd[n];

  /* Zero out the receive descriptors status. */
  desc->status = 0;
}

void lem_rxd_setup(struct adapter* adapter) {
  struct e1000_rx_desc* rxd = adapter->rxd;

  for (int n = 0; n < ETH_RXBUF_COUNT; n++) {
    rxd[n].buffer_addr = adapter->rxb_phys + ETH_RXBUF_SIZE * n;
    /* status bits must be cleared */
    rxd[n].status = 0;
  }
}

struct txrx_funcs lem_txrx = {
    .eth_rx = lem_eth_rx, .eth_rx_ack = lem_eth_rx_ack, .rxd_setup = lem_rxd_setup};

bool e1000_status_online(struct adapter* adapter) {
  return E1000_READ_REG(&adapter->hw, E1000_STATUS) & E1000_STATUS_LU;
}

static int e1000_irq_thread(void* arg) {
  struct adapter* adapter = arg;
  struct e1000_hw* hw = &adapter->hw;
  for (;;) {
    zx_status_t r;
    r = zx_interrupt_wait(adapter->irqh, NULL);
    if (r != ZX_OK) {
      DEBUGOUT("irq wait failed? %d\n", r);
      break;
    }
    mtx_lock(&adapter->lock);
    unsigned irq = E1000_READ_REG(hw, E1000_ICR);
    if (irq & E1000_ICR_RXT0) {
      void* data;
      size_t len;

      while (adapter->txrx->eth_rx(adapter, &data, &len) == ZX_OK) {
        if (adapter->ifc.ops && (adapter->state == ETH_RUNNING)) {
          ethernet_ifc_recv(&adapter->ifc, data, len, 0);
        }
        adapter->txrx->eth_rx_ack(adapter);
        uint32_t n = adapter->rx_rd_ptr;
        E1000_WRITE_REG(&adapter->hw, E1000_RDT(0), n);
        n = (n + 1) & (ETH_RXBUF_COUNT - 1);
        adapter->rx_rd_ptr = n;
      }
    }
    if (irq & E1000_ICR_LSC) {
      bool was_online = adapter->online;
      bool online = e1000_status_online(adapter);
      DEBUGOUT("ETH_IRQ_LSC fired: %d->%d\n", was_online, online);
      if (online != was_online) {
        adapter->online = online;
        if (adapter->ifc.ops) {
          ethernet_ifc_status(&adapter->ifc, online ? ETHERNET_STATUS_ONLINE : 0);
        }
      }
    }
    mtx_unlock(&adapter->lock);
  }
  return 0;
}

static zx_status_t e1000_query(void* ctx, uint32_t options, ethernet_info_t* info) {
  struct adapter* adapter = ctx;

  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  memset(info, 0, sizeof *info);
  info->mtu = ETH_MTU;
  memcpy(info->mac, adapter->hw.mac.addr, sizeof adapter->hw.mac.addr);
  info->netbuf_size = sizeof(ethernet_netbuf_t);

  return ZX_OK;
}

static void e1000_stop(void* ctx) {
  struct adapter* adapter = ctx;
  mtx_lock(&adapter->lock);
  adapter->ifc.ops = NULL;
  mtx_unlock(&adapter->lock);
}

static zx_status_t e1000_start(void* ctx, const ethernet_ifc_protocol_t* ifc) {
  struct adapter* adapter = ctx;
  zx_status_t status = ZX_OK;

  mtx_lock(&adapter->lock);
  if (adapter->ifc.ops) {
    status = ZX_ERR_BAD_STATE;
  } else {
    adapter->ifc = *ifc;
    ethernet_ifc_status(&adapter->ifc, adapter->online ? ETHERNET_STATUS_ONLINE : 0);
  }
  mtx_unlock(&adapter->lock);

  return status;
}

zx_status_t eth_tx(struct adapter* adapter, const void* data, size_t len) {
  if (len > ETH_TXBUF_DSIZE) {
    DEBUGOUT("unsupported packet length %zu\n", len);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ZX_OK;

  mtx_lock(&adapter->send_lock);

  reap_tx_buffers(adapter);

  // obtain buffer, copy into it, setup descriptor
  struct framebuf* frame = list_remove_head_type(&adapter->free_frames, struct framebuf, node);
  if (frame == NULL) {
    status = ZX_ERR_NO_RESOURCES;
    goto out;
  }

  uint32_t n = adapter->tx_wr_ptr;
  memcpy(frame->data, data, len);
  // Pad out short packets.
  if (len < 60) {
    memset(frame->data + len, 0, 60 - len);
    len = 60;
  }
  struct e1000_tx_desc* desc = &adapter->txd[n];
  desc->buffer_addr = frame->phys;
  desc->lower.data = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS | len;
  list_add_tail(&adapter->busy_frames, &frame->node);

  // inform hw of buffer availability
  n = (n + 1) & (ETH_TXBUF_COUNT - 1);
  adapter->tx_wr_ptr = n;
  E1000_WRITE_REG(&adapter->hw, E1000_TDT(0), n);

out:
  mtx_unlock(&adapter->send_lock);
  return status;
}

static void e1000_queue_tx(void* ctx, uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  struct adapter* adapter = ctx;
  if (adapter->state != ETH_RUNNING) {
    completion_cb(cookie, ZX_ERR_BAD_STATE, netbuf);
    return;
  }
  // TODO: Add support for DMA directly from netbuf
  zx_status_t status = eth_tx(adapter, netbuf->data_buffer, netbuf->data_size);
  completion_cb(cookie, status, netbuf);
}

static zx_status_t e1000_set_param(void* ctx, uint32_t param, int32_t value, const void* data,
                                   size_t data_size) {
  return ZX_OK;
}

static ethernet_impl_protocol_ops_t e1000_ethernet_impl_ops = {.query = e1000_query,
                                                               .stop = e1000_stop,
                                                               .start = e1000_start,
                                                               .queue_tx = e1000_queue_tx,
                                                               .set_param = e1000_set_param};

static void e1000_identify_hardware(struct adapter* adapter) {
  pci_protocol_t* pci = &adapter->osdep.pci;

  /* Make sure our PCI config space has the necessary stuff set */
  pci_config_read16(pci, PCI_CONFIG_COMMAND, &adapter->hw.bus.pci_cmd_word);

  /* Save off the information about this board */
  zx_pcie_device_info_t pci_info;
  zx_status_t status = pci_get_device_info(pci, &pci_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pci_get_device_info failure");
    return;
  }

  adapter->hw.vendor_id = pci_info.vendor_id;
  adapter->hw.device_id = pci_info.device_id;
  adapter->hw.revision_id = pci_info.revision_id;
  pci_config_read16(pci, PCI_CONFIG_SUBSYS_VENDOR_ID, &adapter->hw.subsystem_vendor_id);
  pci_config_read16(pci, PCI_CONFIG_SUBSYS_ID, &adapter->hw.subsystem_device_id);

  /* Do Shared Code Init and Setup */
  if (e1000_set_mac_type(&adapter->hw)) {
    zxlogf(ERROR, "e1000_set_mac_type init failure");
    return;
  }
}

static zx_status_t e1000_allocate_pci_resources(struct adapter* adapter) {
  pci_protocol_t* pci = &adapter->osdep.pci;

  zx_status_t status =
      pci_map_bar_buffer(pci, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &adapter->bar0_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pci_map_bar cannot map io %d", status);
    return status;
  }

  adapter->osdep.membase = (uintptr_t)adapter->bar0_mmio.vaddr;
  adapter->hw.hw_addr = (u8*)&adapter->osdep.membase;

  /* Only older adapters use IO mapping */
  uint32_t iorid;
  if (adapter->hw.mac.type < em_mac_min && adapter->hw.mac.type > e1000_82543) {
    /* Figure our where our IO BAR is ? */
    uint32_t rid;
    uint32_t val;
    for (rid = PCI_CONFIG_BASE_ADDRESSES; rid < PCI_CONFIG_CARDBUS_CIS_PTR;) {
      pci_config_read32(pci, rid, &val);

      if (EM_BAR_TYPE(val) == EM_BAR_TYPE_IO) {
        iorid = (rid - PCI_CONFIG_BASE_ADDRESSES) / 4;
        break;
      }
      rid += 4;
      /* check for 64bit BAR */
      if (EM_BAR_MEM_TYPE(val) == EM_BAR_MEM_TYPE_64BIT)
        rid += 4;
    }
    if (rid >= PCI_CONFIG_CARDBUS_CIS_PTR) {
      zxlogf(ERROR, "Unable to locate IO BAR");
      return ZX_ERR_IO_NOT_PRESENT;
    }

    zx_pci_bar_t bar;
    status = pci_get_bar(pci, iorid, &bar);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Unable to allocate bus resource: ioport (%d)", status);
    }

    adapter->osdep.iobase = bar.addr;
    adapter->hw.io_base = 0;
  }

  adapter->hw.back = &adapter->osdep;

  return ZX_OK;
}

void e1000_setup_buffers(struct adapter* adapter, void* iomem, zx_paddr_t iophys) {
  DEBUGOUT("iomem @%p (phys %" PRIxPTR ")\n", iomem, iophys);

  list_initialize(&adapter->free_frames);
  list_initialize(&adapter->busy_frames);

  adapter->rxd = iomem;
  adapter->rxd_phys = iophys;
  iomem += ETH_DRING_SIZE;
  iophys += ETH_DRING_SIZE;
  memset(adapter->rxd, 0, ETH_DRING_SIZE);

  adapter->txd = iomem;
  adapter->txd_phys = iophys;
  iomem += ETH_DRING_SIZE;
  iophys += ETH_DRING_SIZE;
  memset(adapter->txd, 0, ETH_DRING_SIZE);

  adapter->rxb = iomem;
  adapter->rxb_phys = iophys;
  iomem += ETH_RXBUF_SIZE * ETH_RXBUF_COUNT;
  iophys += ETH_RXBUF_SIZE * ETH_RXBUF_COUNT;

  adapter->rxh = iomem;
  adapter->rxh_phys = iophys;
  iomem += ETH_RXHDR_SIZE * ETH_RXBUF_COUNT;
  iophys += ETH_RXHDR_SIZE * ETH_RXBUF_COUNT;

  adapter->txrx->rxd_setup(adapter);

  for (int n = 0; n < ETH_TXBUF_COUNT - 1; n++) {
    struct framebuf* txb = iomem;
    txb->phys = iophys + ETH_TXBUF_HSIZE;
    txb->size = ETH_TXBUF_SIZE - ETH_TXBUF_HSIZE;
    txb->data = iomem + ETH_TXBUF_HSIZE;
    list_add_tail(&adapter->free_frames, &txb->node);

    iomem += ETH_TXBUF_SIZE;
    iophys += ETH_TXBUF_SIZE;
  }
}

static void em_initialize_transmit_unit(struct adapter* adapter) {
  struct e1000_hw* hw = &adapter->hw;
  u32 tctl, txdctl = 0, tarc, tipg = 0;

  DEBUGOUT("em_initialize_transmit_unit: begin");

  u64 bus_addr = adapter->txd_phys;

  /* Base and Len of TX Ring */
  E1000_WRITE_REG(hw, E1000_TDLEN(0), ETH_TXBUF_COUNT * sizeof(struct e1000_tx_desc));
  E1000_WRITE_REG(hw, E1000_TDBAH(0), (u32)(bus_addr >> 32));
  E1000_WRITE_REG(hw, E1000_TDBAL(0), (u32)bus_addr);
  /* Init the HEAD/TAIL indices */
  E1000_WRITE_REG(hw, E1000_TDT(0), 0);
  E1000_WRITE_REG(hw, E1000_TDH(0), 0);

  DEBUGOUT("Base = %x, Length = %x\n", E1000_READ_REG(&adapter->hw, E1000_TDBAL(0)),
           E1000_READ_REG(&adapter->hw, E1000_TDLEN(0)));

  txdctl = 0;        /* clear txdctl */
  txdctl |= 0x1f;    /* PTHRESH */
  txdctl |= 1 << 8;  /* HTHRESH */
  txdctl |= 1 << 16; /* WTHRESH */
  txdctl |= 1 << 22; /* Reserved bit 22 must always be 1 */
  txdctl |= E1000_TXDCTL_GRAN;
  txdctl |= 1 << 25; /* LWTHRESH */

  E1000_WRITE_REG(hw, E1000_TXDCTL(0), txdctl);

  /* Set the default values for the Tx Inter Packet Gap timer */
  switch (adapter->hw.mac.type) {
    case e1000_80003es2lan:
      tipg = DEFAULT_82543_TIPG_IPGR1;
      tipg |= DEFAULT_80003ES2LAN_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
      break;
    case e1000_82542:
      tipg = DEFAULT_82542_TIPG_IPGT;
      tipg |= DEFAULT_82542_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
      tipg |= DEFAULT_82542_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
      break;
    default:
      if ((adapter->hw.phy.media_type == e1000_media_type_fiber) ||
          (adapter->hw.phy.media_type == e1000_media_type_internal_serdes))
        tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
      else
        tipg = DEFAULT_82543_TIPG_IPGT_COPPER;
      tipg |= DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
      tipg |= DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
  }

  E1000_WRITE_REG(&adapter->hw, E1000_TIPG, tipg);
  E1000_WRITE_REG(&adapter->hw, E1000_TIDV, 0);

  if (adapter->hw.mac.type >= e1000_82540)
    E1000_WRITE_REG(&adapter->hw, E1000_TADV, 0);

  if ((adapter->hw.mac.type == e1000_82571) || (adapter->hw.mac.type == e1000_82572)) {
    tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(0));
    tarc |= TARC_SPEED_MODE_BIT;
    E1000_WRITE_REG(&adapter->hw, E1000_TARC(0), tarc);
  } else if (adapter->hw.mac.type == e1000_80003es2lan) {
    /* errata: program both queues to unweighted RR */
    tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(0));
    tarc |= 1;
    E1000_WRITE_REG(&adapter->hw, E1000_TARC(0), tarc);
    tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(1));
    tarc |= 1;
    E1000_WRITE_REG(&adapter->hw, E1000_TARC(1), tarc);
  } else if (adapter->hw.mac.type == e1000_82574) {
    tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(0));
    tarc |= TARC_ERRATA_BIT;
    E1000_WRITE_REG(&adapter->hw, E1000_TARC(0), tarc);
  }

  /* Program the Transmit Control Register */
  tctl = E1000_READ_REG(&adapter->hw, E1000_TCTL);
  tctl &= ~E1000_TCTL_CT;
  tctl |= (E1000_TCTL_PSP | E1000_TCTL_RTLC | E1000_TCTL_EN |
           (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT));

  if (adapter->hw.mac.type >= e1000_82571)
    tctl |= E1000_TCTL_MULR;

  /* This write will effectively turn on the transmit unit. */
  E1000_WRITE_REG(&adapter->hw, E1000_TCTL, tctl);

  /* SPT and KBL errata workarounds */
  if (hw->mac.type == e1000_pch_spt) {
    u32 reg;
    reg = E1000_READ_REG(hw, E1000_IOSFPC);
    reg |= E1000_RCTL_RDMTS_HEX;
    E1000_WRITE_REG(hw, E1000_IOSFPC, reg);
    /* i218-i219 Specification Update 1.5.4.5 */
    reg = E1000_READ_REG(hw, E1000_TARC(0));
    reg &= ~E1000_TARC0_CB_MULTIQ_3_REQ;
    reg |= E1000_TARC0_CB_MULTIQ_2_REQ;
    E1000_WRITE_REG(hw, E1000_TARC(0), reg);
  }
}

static void em_initialize_receive_unit(struct adapter* adapter) {
  struct e1000_hw* hw = &adapter->hw;
  u32 rctl, rxcsum, rfctl;

  /*
   * Make sure receives are disabled while setting
   * up the descriptor ring
   */
  rctl = E1000_READ_REG(hw, E1000_RCTL);
  /* Do not disable if ever enabled on this hardware */
  if ((hw->mac.type != e1000_82574) && (hw->mac.type != e1000_82583)) {
    E1000_WRITE_REG(hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);
  }

  /* Setup the Receive Control Register */
  rctl &= ~(3 << E1000_RCTL_MO_SHIFT);
  rctl |= E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
          (hw->mac.mc_filter_type << E1000_RCTL_MO_SHIFT);

  /* Do not store bad packets */
  rctl &= ~E1000_RCTL_SBP;

  /* Disable Long Packet receive */
  rctl &= ~E1000_RCTL_LPE;

  /* Strip the CRC */
  rctl |= E1000_RCTL_SECRC;

  if (adapter->hw.mac.type >= e1000_82540) {
    E1000_WRITE_REG(&adapter->hw, E1000_RADV, EM_RADV);

    /*
     * Set the interrupt throttling rate. Value is calculated
     * as DEFAULT_ITR = 1/(MAX_INTS_PER_SEC * 256ns)
     */
    E1000_WRITE_REG(hw, E1000_ITR, DEFAULT_ITR);
  }
  E1000_WRITE_REG(&adapter->hw, E1000_RDTR, EM_RDTR);

  /* Use extended rx descriptor formats */
  rfctl = E1000_READ_REG(hw, E1000_RFCTL);
  rfctl |= E1000_RFCTL_EXTEN;
  /*
   * When using MSIX interrupts we need to throttle
   * using the EITR register (82574 only)
   */
  if (hw->mac.type == e1000_82574) {
    for (int i = 0; i < 4; i++) {
      E1000_WRITE_REG(hw, E1000_EITR_82574(i), DEFAULT_ITR);
    }

    /* Disable accelerated acknowledge */
    rfctl |= E1000_RFCTL_ACK_DIS;
  }
  E1000_WRITE_REG(hw, E1000_RFCTL, rfctl);

  rxcsum = E1000_READ_REG(hw, E1000_RXCSUM);
  rxcsum &= ~E1000_RXCSUM_TUOFL;

  E1000_WRITE_REG(hw, E1000_RXCSUM, rxcsum);

  /*
   * XXX TEMPORARY WORKAROUND: on some systems with 82573
   * long latencies are observed, like Lenovo X60. This
   * change eliminates the problem, but since having positive
   * values in RDTR is a known source of problems on other
   * platforms another solution is being sought.
   */
  if (hw->mac.type == e1000_82573) {
    E1000_WRITE_REG(hw, E1000_RDTR, 0x20);
  }

  /* Setup the Base and Length of the Rx Descriptor Ring */
  u64 bus_addr = adapter->rxd_phys;
  adapter->rx_rd_ptr = 0;
  E1000_WRITE_REG(hw, E1000_RDLEN(0), ETH_RXBUF_COUNT * sizeof(union e1000_rx_desc_extended));
  E1000_WRITE_REG(hw, E1000_RDBAH(0), (u32)(bus_addr >> 32));
  E1000_WRITE_REG(hw, E1000_RDBAL(0), (u32)bus_addr);

  /*
   * Set PTHRESH for improved jumbo performance
   * According to 10.2.5.11 of Intel 82574 Datasheet,
   * RXDCTL(1) is written whenever RXDCTL(0) is written.
   * Only write to RXDCTL(1) if there is a need for different
   * settings.
   */

  if (adapter->hw.mac.type == e1000_82574) {
    u32 rxdctl = E1000_READ_REG(hw, E1000_RXDCTL(0));
    rxdctl |= 0x20;    /* PTHRESH */
    rxdctl |= 4 << 8;  /* HTHRESH */
    rxdctl |= 4 << 16; /* WTHRESH */
    rxdctl |= 1 << 24; /* Switch to granularity */
    E1000_WRITE_REG(hw, E1000_RXDCTL(0), rxdctl);
  } else if (adapter->hw.mac.type >= igb_mac_min) {
    u32 srrctl = 2048 >> E1000_SRRCTL_BSIZEPKT_SHIFT;
    rctl |= E1000_RCTL_SZ_2048;

    /* Setup the Base and Length of the Rx Descriptor Rings */
    bus_addr = adapter->rxd_phys;
    u32 rxdctl;

    srrctl |= E1000_SRRCTL_DESCTYPE_ADV_ONEBUF;

    E1000_WRITE_REG(hw, E1000_RDLEN(0), ETH_RXBUF_COUNT * sizeof(struct e1000_rx_desc));
    E1000_WRITE_REG(hw, E1000_RDBAH(0), (uint32_t)(bus_addr >> 32));
    E1000_WRITE_REG(hw, E1000_RDBAL(0), (uint32_t)bus_addr);
    E1000_WRITE_REG(hw, E1000_SRRCTL(0), srrctl);
    /* Enable this Queue */
    rxdctl = E1000_READ_REG(hw, E1000_RXDCTL(0));
    rxdctl |= E1000_RXDCTL_QUEUE_ENABLE;
    rxdctl &= 0xFFF00000;
    rxdctl |= IGB_RX_PTHRESH;
    rxdctl |= IGB_RX_HTHRESH << 8;
    rxdctl |= IGB_RX_WTHRESH << 16;
    E1000_WRITE_REG(hw, E1000_RXDCTL(0), rxdctl);

    /* poll for enable completion */
    do {
      rxdctl = E1000_READ_REG(hw, E1000_RXDCTL(0));
    } while (!(rxdctl & E1000_RXDCTL_QUEUE_ENABLE));

  } else if (adapter->hw.mac.type >= e1000_pch2lan) {
    e1000_lv_jumbo_workaround_ich8lan(hw, FALSE);
  }

  /* Make sure VLAN Filters are off */
  rctl &= ~E1000_RCTL_VFE;

  if (adapter->hw.mac.type < igb_mac_min) {
    rctl |= E1000_RCTL_SZ_2048;
    /* ensure we clear use DTYPE of 00 here */
    rctl &= ~0x00000C00;
  }

  /* Setup the Head and Tail Descriptor Pointers */
  E1000_WRITE_REG(hw, E1000_RDH(0), 0);
  E1000_WRITE_REG(hw, E1000_RDT(0), ETH_RXBUF_COUNT - 1);

  /* Write out the settings */
  E1000_WRITE_REG(hw, E1000_RCTL, rctl);

  return;
}

static void em_disable_promisc(struct adapter* adapter) {
  u32 reg_rctl;

  reg_rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);
  reg_rctl &= (~E1000_RCTL_UPE);
  reg_rctl &= (~E1000_RCTL_SBP);
  E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
}

static int em_if_set_promisc(struct adapter* adapter, int flags) {
  u32 reg_rctl;

  em_disable_promisc(adapter);

  reg_rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);

  if (flags & IFF_PROMISC) {
    reg_rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
    E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
  } else if (flags & IFF_ALLMULTI) {
    reg_rctl |= E1000_RCTL_MPE;
    reg_rctl &= ~E1000_RCTL_UPE;
    E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
  }
  return (0);
}

static zx_status_t e1000_bind(void* ctx, zx_device_t* dev) {
  DEBUGOUT("bind entry\n");

  struct adapter* adapter = calloc(1, sizeof *adapter);
  if (!adapter) {
    return ZX_ERR_NO_MEMORY;
  }

  mtx_init(&adapter->lock, mtx_plain);
  mtx_init(&adapter->send_lock, mtx_plain);

  zx_status_t status = device_get_protocol(dev, ZX_PROTOCOL_PCI, &adapter->osdep.pci);
  if (status != ZX_OK) {
    zxlogf(ERROR, "no pci protocol (%d)", status);
    goto fail;
  }

  pci_protocol_t* pci = &adapter->osdep.pci;

  status = pci_enable_bus_master(pci, true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "cannot enable bus master %d", status);
    goto fail;
  }

  status = pci_get_bti(pci, 0, &adapter->btih);
  if (status != ZX_OK) {
    goto fail;
  }

  // Request 1 interrupt of any mode.
  status = pci_configure_irq_mode(pci, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to configure irqs");
    goto fail;
  }

  status = pci_map_interrupt(pci, 0, &adapter->irqh);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to map irq");
    goto fail;
  }

  e1000_identify_hardware(adapter);
  status = e1000_allocate_pci_resources(adapter);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Allocation of PCI resources failed (%d)", status);
    goto fail;
  }

  if (adapter->hw.mac.type >= igb_mac_min) {
    adapter->txrx = &igb_txrx;
  } else if (adapter->hw.mac.type >= em_mac_min) {
    adapter->txrx = &em_txrx;
  } else {
    adapter->txrx = &lem_txrx;
  }

  struct e1000_hw* hw = &adapter->hw;

  /*
  ** For ICH8 and family we need to
  ** map the flash memory, and this
  ** must happen after the MAC is
  ** identified
  */
  if ((hw->mac.type == e1000_ich8lan) || (hw->mac.type == e1000_ich9lan) ||
      (hw->mac.type == e1000_ich10lan) || (hw->mac.type == e1000_pchlan) ||
      (hw->mac.type == e1000_pch2lan) || (hw->mac.type == e1000_pch_lpt)) {
    status = pci_map_bar_buffer(pci, EM_BAR_TYPE_FLASH / 4, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                &adapter->flash_mmio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Mapping of Flash failed");
      goto fail;
    }
    /* This is used in the shared code */
    // TODO(fxbug.dev/56253): Add MMIO_PTR to cast.
    hw->flash_address = (void*)adapter->flash_mmio.vaddr;
    adapter->osdep.flashbase = (uintptr_t)adapter->flash_mmio.vaddr;
  }
  /*
  ** In the new SPT device flash is not  a
  ** separate BAR, rather it is also in BAR0,
  ** so use the same tag and an offset handle for the
  ** FLASH read/write macros in the shared code.
  */
  else if (hw->mac.type >= e1000_pch_spt) {
    adapter->osdep.flashbase = adapter->osdep.membase + E1000_FLASH_BASE_ADDR;
  }

  s32 err = e1000_setup_init_funcs(hw, TRUE);
  if (err) {
    zxlogf(ERROR, "Setup of Shared code failed, error %d", err);
    goto fail;
  }

  e1000_get_bus_info(hw);

  hw->mac.autoneg = 1;
  hw->phy.autoneg_wait_to_complete = FALSE;
  hw->phy.autoneg_advertised = (ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF |
                                ADVERTISE_100_FULL | ADVERTISE_1000_FULL);

  /* Copper options */
  if (hw->phy.media_type == e1000_media_type_copper) {
    hw->phy.mdix = 0;
    hw->phy.disable_polarity_correction = FALSE;
    hw->phy.ms_type = e1000_ms_hw_default;
  }

  /*
   * This controls when hardware reports transmit completion
   * status.
   */
  hw->mac.report_tx_early = 1;

  /* Check SOL/IDER usage */
  if (e1000_check_reset_block(hw)) {
    DEBUGOUT("PHY reset is blocked due to SOL/IDER session.\n");
  }

  /*
  ** Start from a known state, this is
  ** important in reading the nvm and
  ** mac from that.eth_queue_tx
  */
  e1000_reset_hw(hw);
  e1000_power_up_phy(hw);

  /* Make sure we have a good EEPROM before we read from it */
  if (e1000_validate_nvm_checksum(hw) < 0) {
    /*
    ** Some PCI-E parts fail the first check due to
    ** the link being in sleep state, call it again,
    ** if it fails a second time its a real issue.
    */
    if (e1000_validate_nvm_checksum(hw) < 0) {
      zxlogf(ERROR, "The EEPROM Checksum Is Not Valid");
      goto fail;
    }
  }

  /* Copy the permanent MAC address out of the EEPROM */
  if (e1000_read_mac_addr(hw) < 0) {
    zxlogf(ERROR, "EEPROM read error while reading MAC address");
    goto fail;
  }

  DEBUGOUT("MAC address %02x:%02x:%02x:%02x:%02x:%02x\n", hw->mac.addr[0], hw->mac.addr[1],
           hw->mac.addr[2], hw->mac.addr[3], hw->mac.addr[4], hw->mac.addr[5]);

  /* Disable ULP support */
  e1000_disable_ulp_lpt_lp(hw, TRUE);

  status =
      io_buffer_init(&adapter->buffer, adapter->btih, ETH_ALLOC, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status < 0) {
    zxlogf(ERROR, "cannot alloc io-buffer %d", status);
    goto fail;
  }

  e1000_setup_buffers(adapter, io_buffer_virt(&adapter->buffer), io_buffer_phys(&adapter->buffer));

  /* Prepare transmit descriptors and buffers */
  em_initialize_transmit_unit(adapter);

  // setup rx ring
  em_initialize_receive_unit(adapter);

  /* Don't lose promiscuous settings */
  em_if_set_promisc(adapter, IFF_PROMISC);
  e1000_clear_hw_cntrs_base_generic(hw);

  adapter->online = e1000_status_online(adapter);

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "e1000",
      .ctx = adapter,
      .ops = &e1000_device_ops,
      .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
      .proto_ops = &e1000_ethernet_impl_ops,
  };

  if (device_add(dev, &args, &adapter->zxdev)) {
    goto fail;
  }

  thrd_create_with_name(&adapter->thread, e1000_irq_thread, adapter, "e1000_irq_thread");
  thrd_detach(adapter->thread);

  // enable interrupts
  u32 ims_mask = IMS_ENABLE_MASK;
  E1000_WRITE_REG(hw, E1000_IMS, ims_mask);

  DEBUGOUT("online\n");
  return ZX_OK;

fail:
  io_buffer_release(&adapter->buffer);
  if (adapter->btih) {
    zx_handle_close(adapter->btih);
  }
  if (adapter->osdep.pci.ops) {
    pci_enable_bus_master(&adapter->osdep.pci, false);
  }
  zx_handle_close(adapter->irqh);
  mmio_buffer_release(&adapter->bar0_mmio);
  mmio_buffer_release(&adapter->flash_mmio);
  free(adapter);
  zxlogf(ERROR, "%s: %d FAIL", __FUNCTION__, __LINE__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_driver_ops_t e1000_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = e1000_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(e1000, e1000_driver_ops, "zircon", "0.1", 157)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82542),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82543GC_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82543GC_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82544EI_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82544EI_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82544GC_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82544GC_LOM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82540EM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82540EM_LOM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82540EP_LOM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82540EP),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82540EP_LP),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82545EM_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82545EM_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82545GM_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82545GM_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82545GM_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546EB_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546EB_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546EB_QUAD_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546GB_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546GB_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546GB_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546GB_PCIE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546GB_QUAD_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82541EI),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82541EI_MOBILE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82541ER_LOM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82541ER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82541GI),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82541GI_LF),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82541GI_MOBILE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82547EI),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82547EI_MOBILE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82547GI),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571EB_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571EB_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571EB_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571EB_SERDES_DUAL),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571EB_SERDES_QUAD),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571EB_QUAD_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571PT_QUAD_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571EB_QUAD_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82571EB_QUAD_COPPER_LP),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82572EI_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82572EI_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82572EI_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82572EI),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82573E),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82573E_IAMT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82573L),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82574L),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82574LA),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82583V),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_80003ES2LAN_COPPER_DPT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_80003ES2LAN_SERDES_DPT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_80003ES2LAN_COPPER_SPT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_80003ES2LAN_SERDES_SPT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH8_82567V_3),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH8_IGP_M_AMT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH8_IGP_AMT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH8_IGP_C),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH8_IFE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH8_IFE_GT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH8_IFE_G),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH8_IGP_M),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_IGP_M),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_IGP_M_AMT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_IGP_M_V),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_IGP_AMT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_BM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_IGP_C),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_IFE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_IFE_GT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH9_IFE_G),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH10_R_BM_LM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH10_R_BM_LF),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH10_R_BM_V),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH10_D_BM_LM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH10_D_BM_LF),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_ICH10_D_BM_V),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_M_HV_LM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_M_HV_LC),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_D_HV_DM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_D_HV_DC),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH2_LV_LM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH2_LV_V),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_LPT_I217_LM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_LPT_I217_V),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_LPTLP_I218_LM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_LPTLP_I218_V),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_I218_LM2),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_I218_V2),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_I218_LM3),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_I218_V3),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_SPT_I219_LM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_SPT_I219_V),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_SPT_I219_LM2),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_SPT_I219_V2),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_LBG_I219_LM3),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_SPT_I219_LM4),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_SPT_I219_V4),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_SPT_I219_LM5),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_SPT_I219_V5),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_CNP_I219_LM6),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_CNP_I219_V6),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_CNP_I219_LM7),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_CNP_I219_V7),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_ICP_I219_LM8),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_ICP_I219_V8),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_ICP_I219_LM9),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_PCH_ICP_I219_V9),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_QUAD_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_QUAD_COPPER_ET2),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_NS),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_NS_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_SERDES_QUAD),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_VF),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82576_VF_HV),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I350_VF),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I350_VF_HV),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82575EB_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82575EB_FIBER_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82575GB_QUAD_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82580_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82580_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82580_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82580_SGMII),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82580_COPPER_DUAL),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_82580_QUAD_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I350_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I350_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I350_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I350_SGMII),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I350_DA4),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I210_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I210_COPPER_OEM1),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I210_COPPER_IT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I210_FIBER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I210_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I210_SGMII),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I210_COPPER_FLASHLESS),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I210_SERDES_FLASHLESS),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I211_COPPER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I354_BACKPLANE_1GBPS),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I354_SGMII),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_I354_BACKPLANE_2_5GBPS),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_DH89XXCC_SGMII),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_DH89XXCC_SERDES),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_DH89XXCC_BACKPLANE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, E1000_DEV_ID_DH89XXCC_SFP),
ZIRCON_DRIVER_END(e1000)
