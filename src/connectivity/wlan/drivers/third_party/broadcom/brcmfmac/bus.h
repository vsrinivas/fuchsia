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

#include "core.h"
#include "device.h"
#include "netbuf.h"

// HW/SW bus in use
enum brcmf_bus_type { BRCMF_BUS_TYPE_SDIO, BRCMF_BUS_TYPE_SIM, BRCMF_BUS_TYPE_USB };

/* The level of bus communication with the dongle */
enum brcmf_bus_state {
  BRCMF_BUS_DOWN, /* Not ready for frame transfers */
  BRCMF_BUS_UP    /* Ready for frame transfers */
};

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
 * @device_add: register device.
 *
 * This structure provides an abstract interface towards the
 * bus specific driver. For control messages to common driver
 * will assure there is only one active transaction. Unless
 * indicated otherwise these callbacks are mandatory.
 */

#include "device.h"

struct brcmf_bus_ops {
  enum brcmf_bus_type (*get_bus_type)();
  zx_status_t (*preinit)(struct brcmf_device* dev);
  void (*stop)(struct brcmf_device* dev);
  zx_status_t (*txdata)(struct brcmf_device* dev, struct brcmf_netbuf* netbuf);
  zx_status_t (*txctl)(struct brcmf_device* dev, unsigned char* msg, uint len);
  zx_status_t (*rxctl)(struct brcmf_device* dev, unsigned char* msg, uint len, int* rxlen_out);
  struct pktq* (*gettxq)(struct brcmf_device* dev);
  void (*wowl_config)(struct brcmf_device* dev, bool enabled);
  size_t (*get_ramsize)(struct brcmf_device* dev);
  zx_status_t (*get_memdump)(struct brcmf_device* dev, void* data, size_t len);
  zx_status_t (*get_fwname)(struct brcmf_device* dev, uint chip, uint chiprev,
                            unsigned char* fw_name);
  zx_status_t (*get_bootloader_macaddr)(struct brcmf_device* dev, uint8_t* mac_addr);
  zx_status_t (*device_add)(zx_device_t* parent, device_add_args_t* args, zx_device_t** out);
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
    struct brcmf_sdio_dev* sdio;
    struct brcmf_usbdev* usb;
    struct brcmf_pciedev* pcie;
    struct brcmf_simdev* sim;
  } bus_priv;
  struct brcmf_device* dev;
  std::unique_ptr<struct brcmf_pub> drvr;
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
static inline zx_status_t brcmf_bus_preinit(struct brcmf_bus* bus) {
  if (!bus->ops->preinit) {
    return ZX_OK;
  }
  return bus->ops->preinit(bus->dev);
}

static inline void brcmf_bus_stop(struct brcmf_bus* bus) { bus->ops->stop(bus->dev); }

static inline int brcmf_bus_txdata(struct brcmf_bus* bus, struct brcmf_netbuf* netbuf) {
  return bus->ops->txdata(bus->dev, netbuf);
}

static inline int brcmf_bus_txctl(struct brcmf_bus* bus, unsigned char* msg, uint len) {
  return bus->ops->txctl(bus->dev, msg, len);
}

static inline int brcmf_bus_rxctl(struct brcmf_bus* bus, unsigned char* msg, uint len,
                                  int* rxlen_out) {
  return bus->ops->rxctl(bus->dev, msg, len, rxlen_out);
}

static inline zx_status_t brcmf_bus_gettxq(struct brcmf_bus* bus, struct pktq** txq_out) {
  if (!bus->ops->gettxq) {
    if (txq_out) {
      *txq_out = NULL;
    }
    return ZX_ERR_NOT_FOUND;
  }
  if (txq_out) {
    *txq_out = bus->ops->gettxq(bus->dev);
  }
  return ZX_OK;
}

static inline void brcmf_bus_wowl_config(struct brcmf_bus* bus, bool enabled) {
  if (bus->ops->wowl_config) {
    bus->ops->wowl_config(bus->dev, enabled);
  }
}

static inline size_t brcmf_bus_get_ramsize(struct brcmf_bus* bus) {
  if (!bus->ops->get_ramsize) {
    return 0;
  }

  return bus->ops->get_ramsize(bus->dev);
}

static inline zx_status_t brcmf_bus_get_memdump(struct brcmf_bus* bus, void* data, size_t len) {
  if (!bus->ops->get_memdump) {
    return ZX_ERR_NOT_FOUND;
  }

  return bus->ops->get_memdump(bus->dev, data, len);
}

static inline zx_status_t brcmf_bus_get_fwname(struct brcmf_bus* bus, uint chip, uint chiprev,
                                               unsigned char* fw_name) {
  return bus->ops->get_fwname(bus->dev, chip, chiprev, fw_name);
}

static inline zx_status_t brcmf_bus_get_bootloader_macaddr(struct brcmf_bus* bus,
                                                           uint8_t* mac_addr) {
  return bus->ops->get_bootloader_macaddr(bus->dev, mac_addr);
}

static inline zx_status_t brcmf_bus_device_add(struct brcmf_bus* bus, zx_device_t* parent,
                                               device_add_args_t* args, zx_device_t** out) {
  return bus->ops->device_add(parent, args, out);
}

/*
 * interface functions from common layer
 */

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
void brcmf_rx_frame(struct brcmf_device* dev, struct brcmf_netbuf* rxp, bool handle_event);
/* Receive async event packet from firmware. Callee disposes of rxp. */
void brcmf_rx_event(struct brcmf_device* dev, struct brcmf_netbuf* rxp);

/* Indication from bus module regarding presence/insertion of dongle. */
zx_status_t brcmf_attach(struct brcmf_device* dev, struct brcmf_mp_device* settings);
/* Indication from bus module regarding removal/absence of dongle */
void brcmf_detach(struct brcmf_device* dev);
/* Indication from bus module that dongle should be reset */
void brcmf_dev_reset(struct brcmf_device* dev);

/* Configure the "global" bus state used by upper layers */
void brcmf_bus_change_state(struct brcmf_bus* bus, enum brcmf_bus_state state);

zx_status_t brcmf_bus_started(struct brcmf_device* dev);
zx_status_t brcmf_iovar_data_set(struct brcmf_device* dev, const char* name, void* data,
                                 uint32_t len, int32_t* fwerr_ptr);
void brcmf_bus_add_txhdrlen(struct brcmf_device* dev, uint len);

// Interface to the system bus.
zx_status_t brcmf_bus_register(struct brcmf_device* device);
void brcmf_bus_exit(struct brcmf_device* device);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BUS_H_
