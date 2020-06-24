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
#include <zircon/time.h>

#include <functional>
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
  zx_status_t (*get_bootloader_macaddr)(brcmf_bus* bus, uint8_t* mac_addr);
  zx_status_t (*get_wifi_metadata)(brcmf_bus* bus, void* config, size_t exp_size, size_t* actual);

  // Deprecated entry points.
  zx_status_t (*preinit)(brcmf_bus* bus);
  void (*stop)(brcmf_bus* bus);
  zx_status_t (*txdata)(brcmf_bus* bus, struct brcmf_netbuf* netbuf);
  zx_status_t (*txctl)(brcmf_bus* bus, unsigned char* msg, uint len);
  zx_status_t (*rxctl)(brcmf_bus* bus, unsigned char* msg, uint len, int* rxlen_out);
  struct pktq* (*gettxq)(brcmf_bus* bus);
  void (*set_sim_timer)(brcmf_bus* bus, std::unique_ptr<std::function<void()>> fn,
                        zx_duration_t delay, uint64_t* id_out);
  void (*cancel_sim_timer)(brcmf_bus* bus, uint64_t id);
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
    if (!(*txq_out))
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

static inline zx_status_t brcmf_bus_get_bootloader_macaddr(struct brcmf_bus* bus,
                                                           uint8_t* mac_addr) {
  return bus->ops->get_bootloader_macaddr(bus, mac_addr);
}

static inline zx_status_t brcmf_bus_get_wifi_metadata(struct brcmf_bus* bus, void* config,
                                                      size_t exp_size, size_t* actual) {
  return bus->ops->get_wifi_metadata(bus, config, exp_size, actual);
}

// Bus operation for simulation test framework.
static inline void brcmf_bus_set_sim_timer(struct brcmf_bus* bus,
                                           std::unique_ptr<std::function<void()>> fn,
                                           zx_duration_t delay, uint64_t* id_out) {
  return bus->ops->set_sim_timer(bus, std::move(fn), delay, id_out);
}

static inline void brcmf_bus_cancel_sim_timer(struct brcmf_bus* bus, uint64_t id) {
  return bus->ops->cancel_sim_timer(bus, id);
}

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BUS_H_
