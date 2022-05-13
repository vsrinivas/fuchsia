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
#include <fuchsia/hardware/network/device/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/stdcompat/span.h>
#include <zircon/time.h>

#include <functional>
#include <memory>

#include <wlan/drivers/components/frame.h>
#include <wlan/drivers/components/frame_container.h>

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
  zx_status_t (*txframes)(brcmf_bus* bus, cpp20::span<wlan::drivers::components::Frame> frames);
  zx_status_t (*txctl)(brcmf_bus* bus, unsigned char* msg, uint len);
  zx_status_t (*rxctl)(brcmf_bus* bus, unsigned char* msg, uint len, int* rxlen_out);
  zx_status_t (*flush_txq)(brcmf_bus* bus, int ifidx);
  zx_status_t (*flush_buffers)(brcmf_bus* bus);
  zx_status_t (*get_tx_depth)(brcmf_bus* bus, uint16_t* tx_depth_out);
  zx_status_t (*get_rx_depth)(brcmf_bus* bus, uint16_t* rx_depth_out);
  zx_status_t (*get_tail_length)(brcmf_bus* bus, uint16_t* tail_length_out);
  zx_status_t (*recovery)(brcmf_bus* bus);
  void (*log_stats)(brcmf_bus* bus);
  zx_status_t (*prepare_vmo)(brcmf_bus* bus, uint8_t vmo_id, zx_handle_t vmo, uint8_t* mapped_addr,
                             size_t mapped_size);
  zx_status_t (*release_vmo)(brcmf_bus* bus, uint8_t vmo_id);
  zx_status_t (*queue_rx_space)(brcmf_bus* bus, const rx_space_buffer_t* buffers_list,
                                size_t buffers_count, uint8_t* vmo_addrs[]);
  wlan::drivers::components::FrameContainer (*acquire_tx_space)(brcmf_bus* bus, size_t count);
};

namespace wlan {
namespace brcmfmac {

class PcieBus;

}  // namespace brcmfmac
}  // namespace wlan

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

static inline zx_status_t brcmf_bus_tx_frames(
    struct brcmf_bus* bus, cpp20::span<wlan::drivers::components::Frame> frames) {
  return bus->ops->txframes(bus, frames);
}

static inline int brcmf_bus_txctl(struct brcmf_bus* bus, unsigned char* msg, uint len) {
  return bus->ops->txctl(bus, msg, len);
}

static inline int brcmf_bus_rxctl(struct brcmf_bus* bus, unsigned char* msg, uint len,
                                  int* rxlen_out) {
  return bus->ops->rxctl(bus, msg, len, rxlen_out);
}

static inline zx_status_t brcmf_bus_get_tx_depth(struct brcmf_bus* bus, uint16_t* tx_depth_out) {
  return bus->ops->get_tx_depth(bus, tx_depth_out);
}

static inline zx_status_t brcmf_bus_get_rx_depth(struct brcmf_bus* bus, uint16_t* rx_depth_out) {
  return bus->ops->get_rx_depth(bus, rx_depth_out);
}

static inline zx_status_t brcmf_bus_get_tail_length(struct brcmf_bus* bus,
                                                   uint16_t* tail_length_out) {
  return bus->ops->get_tail_length(bus, tail_length_out);
}

static inline zx_status_t brcmf_bus_flush_txq(struct brcmf_bus* bus, int ifidx) {
  return bus->ops->flush_txq(bus, ifidx);
}

static inline zx_status_t brcmf_bus_flush_buffers(struct brcmf_bus* bus) {
  return bus->ops->flush_buffers(bus);
}

static inline zx_status_t brcmf_bus_get_bootloader_macaddr(struct brcmf_bus* bus,
                                                           uint8_t* mac_addr) {
  return bus->ops->get_bootloader_macaddr(bus, mac_addr);
}

static inline zx_status_t brcmf_bus_get_wifi_metadata(struct brcmf_bus* bus, void* config,
                                                      size_t exp_size, size_t* actual) {
  return bus->ops->get_wifi_metadata(bus, config, exp_size, actual);
}

static inline zx_status_t brcmf_bus_recovery(struct brcmf_bus* bus) {
  return bus->ops->recovery(bus);
}

static inline void brcmf_bus_log_stats(struct brcmf_bus* bus) { bus->ops->log_stats(bus); }

static inline zx_status_t brcmf_bus_prepare_vmo(struct brcmf_bus* bus, uint8_t vmo_id,
                                                zx_handle_t vmo, uint8_t* mapped_addr,
                                                size_t mapped_size) {
  return bus->ops->prepare_vmo(bus, vmo_id, vmo, mapped_addr, mapped_size);
}

static inline zx_status_t brcmf_bus_release_vmo(struct brcmf_bus* bus, uint8_t vmo_id) {
  return bus->ops->release_vmo(bus, vmo_id);
}

static inline zx_status_t brcmf_bus_queue_rx_space(struct brcmf_bus* bus,
                                                   const rx_space_buffer_t* buffers_list,
                                                   size_t buffers_count, uint8_t* vmo_addrs[]) {
  return bus->ops->queue_rx_space(bus, buffers_list, buffers_count, vmo_addrs);
}

static inline wlan::drivers::components::FrameContainer brcmf_bus_acquire_tx_space(
    struct brcmf_bus* bus, size_t count) {
  return bus->ops->acquire_tx_space(bus, count);
}

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BUS_H_
