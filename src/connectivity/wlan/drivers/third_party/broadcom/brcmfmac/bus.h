/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BUS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BUS_H_

#include <atomic>
#include <memory>

#include <ddk/device.h>
#include <ddk/driver.h>

#include "netbuf.h"

// HW/SW bus in use
enum brcmf_bus_type { BRCMF_BUS_TYPE_SDIO, BRCMF_BUS_TYPE_PCIE, BRCMF_BUS_TYPE_SIM };

/* The level of bus communication with the dongle */
enum brcmf_bus_state {
  BRCMF_BUS_DOWN, /* Not ready for frame transfers */
  BRCMF_BUS_UP    /* Ready for frame transfers */
};

struct brcmf_pub;
struct brcmf_mp_device;

struct brcmf_bus_dcmd {
  const char* name;
  char* param;
  int param_len;
  struct list_node list;
};

/**
 * struct brcmf_bus_ops - bus callback operations.
 *
 * @preinit: execute bus/device specific dongle init commands (optional).
 * @init: prepare for communication with dongle.
 * @stop: clear pending frames, disable data flow.
 * @txdata: send a data frame to the dongle. When the data
 *  has been transferred, the common driver must be
 *  notified using brcmf_txcomplete(). The common
 *  driver calls this function with interrupts
 *  disabled.
 * @txctl: transmit a control request message to dongle.
 * @rxctl: receive a control response message from dongle.
 * @gettxq: obtain a reference of bus transmit queue (optional).
 * @wowl_config: specify if dongle is configured for wowl when going to suspend
 * @get_ramsize: obtain size of device memory.
 * @get_memdump: obtain device memory dump in provided buffer.
 * @get_fwname: obtain firmware name.
 * @get_bootloader_macaddr: obtain mac address from bootloader, if supported.
 *
 * This structure provides an abstract interface towards the
 * bus specific driver. For control messages to common driver
 * will assure there is only one active transaction. Unless
 * indicated otherwise these callbacks are mandatory.
 */

struct brcmf_bus;

struct brcmf_bus_ops {
  enum brcmf_bus_type (*get_bus_type)();
  zx_status_t (*preinit)(brcmf_bus* bus);
  void (*stop)(brcmf_bus* bus);
  zx_status_t (*txdata)(brcmf_bus* bus, struct brcmf_netbuf* netbuf);
  zx_status_t (*txctl)(brcmf_bus* bus, unsigned char* msg, uint len);
  zx_status_t (*rxctl)(brcmf_bus* bus, unsigned char* msg, uint len, int* rxlen_out);
  struct pktq* (*gettxq)(brcmf_bus* bus);
  void (*wowl_config)(brcmf_bus* bus, bool enabled);
  size_t (*get_ramsize)(brcmf_bus* bus);
  zx_status_t (*get_memdump)(brcmf_bus* bus, void* data, size_t len);
  zx_status_t (*get_fwname)(brcmf_bus* bus, uint chip, uint chiprev, unsigned char* fw_name,
                            size_t* fw_name_size);
  zx_status_t (*get_bootloader_macaddr)(brcmf_bus* bus, uint8_t* mac_addr);
  zx_status_t (*get_wifi_metadata)(zx_device_t* zxdev, void* config, size_t exp_size,
                                   size_t* actual);
};

/**
 * struct brcmf_bus_stats - bus statistic counters.
 *
 * @pktcowed: packets cowed for extra headroom/unorphan.
 * @pktcow_failed: packets dropped due to failed cow-ing.
 */
struct brcmf_bus_stats {
  std::atomic<int> pktcowed;
  std::atomic<int> pktcow_failed;
};

namespace wlan {
namespace brcmfmac {

class PcieBus;

}  // namespace brcmfmac
}  // namespace wlan

/**
 * struct brcmf_bus - interface structure between common and bus layer
 *
 * @bus_priv: pointer to private bus device.
 * @dev: device pointer of bus device.
 * @drvr: public driver information.
 * @state: operational state of the bus interface.
 * @stats: statistics shared between common and bus layer.
 * @maxctl: maximum size for rxctl request message.
 * @chip: device identifier of the dongle chip.
 * @always_use_fws_queue: bus wants use queue also when fwsignal is inactive.
 * @wowl_supported: is wowl supported by bus driver.
 * @chiprev: revision of the dongle chip.
 */
struct brcmf_bus {
  union {
    ::wlan::brcmfmac::PcieBus* pcie;
    struct brcmf_sdio_dev* sdio;
    struct brcmf_usbdev* usb;
    struct brcmf_simdev* sim;
  } bus_priv;
  enum brcmf_bus_state state;
  struct brcmf_bus_stats stats;
  uint maxctl;
  uint32_t chip;
  uint32_t chiprev;
  bool always_use_fws_queue;
  bool wowl_supported;

  const struct brcmf_bus_ops* ops;
};

/*
 * callback wrappers
 */
static inline enum brcmf_bus_type brcmf_bus_get_bus_type(struct brcmf_bus* bus) {
  return bus->ops->get_bus_type();
}

static inline zx_status_t brcmf_bus_preinit(struct brcmf_bus* bus) {
  if (!bus->ops->preinit) {
    return ZX_OK;
  }
  return bus->ops->preinit(bus);
}

static inline void brcmf_bus_stop(struct brcmf_bus* bus) { bus->ops->stop(bus); }

static inline int brcmf_bus_txdata(struct brcmf_bus* bus, struct brcmf_netbuf* netbuf) {
  return bus->ops->txdata(bus, netbuf);
}

static inline int brcmf_bus_txctl(struct brcmf_bus* bus, unsigned char* msg, uint len) {
  return bus->ops->txctl(bus, msg, len);
}

static inline int brcmf_bus_rxctl(struct brcmf_bus* bus, unsigned char* msg, uint len,
                                  int* rxlen_out) {
  return bus->ops->rxctl(bus, msg, len, rxlen_out);
}

static inline zx_status_t brcmf_bus_gettxq(struct brcmf_bus* bus, struct pktq** txq_out) {
  if (!bus->ops->gettxq) {
    if (txq_out) {
      *txq_out = NULL;
    }
    return ZX_ERR_NOT_FOUND;
  }
  if (txq_out) {
    *txq_out = bus->ops->gettxq(bus);
  }
  return ZX_OK;
}

static inline void brcmf_bus_wowl_config(struct brcmf_bus* bus, bool enabled) {
  if (bus->ops->wowl_config) {
    bus->ops->wowl_config(bus, enabled);
  }
}

static inline size_t brcmf_bus_get_ramsize(struct brcmf_bus* bus) {
  if (!bus->ops->get_ramsize) {
    return 0;
  }

  return bus->ops->get_ramsize(bus);
}

static inline zx_status_t brcmf_bus_get_memdump(struct brcmf_bus* bus, void* data, size_t len) {
  if (!bus->ops->get_memdump) {
    return ZX_ERR_NOT_FOUND;
  }

  return bus->ops->get_memdump(bus, data, len);
}

static inline zx_status_t brcmf_bus_get_fwname(struct brcmf_bus* bus, uint chip, uint chiprev,
                                               unsigned char* fw_name, size_t* fw_name_size) {
  return bus->ops->get_fwname(bus, chip, chiprev, fw_name, fw_name_size);
}

static inline zx_status_t brcmf_bus_get_bootloader_macaddr(struct brcmf_bus* bus,
                                                           uint8_t* mac_addr) {
  return bus->ops->get_bootloader_macaddr(bus, mac_addr);
}

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BUS_H_
