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
/* ****************** SDIO CARD Interface Functions **************************/

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/sdio/cpp/banjo.h>
#include <inttypes.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/trace/event.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/vmo.h>
#include <pthread.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <atomic>
#include <limits>

#include <wifi/wifi-config.h>

#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#endif
#include <threads.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_utils.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_wifi.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chip.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipcommon.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/defs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/linuxisms.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio/sdio.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/soc.h"

#define SDIOH_API_ACCESS_RETRY_LIMIT 2

#define DMA_ALIGN_MASK 0x03

#define SDIO_FUNC1_BLOCKSIZE 64
// All transfers for func2 will be a multiple of this size, even if only transfering a few bytes
// it will be padded to match this size. Strike a balance between a large block size that's
// beneficial for high throughput with big transfers and small transfers where the padding imposes
// a cost with no benefit.
#define SDIO_FUNC2_BLOCKSIZE 256
/* Maximum milliseconds to wait for F2 to come up */
#define SDIO_WAIT_F2RDY 3000

#define BRCMF_DEFAULT_RXGLOM_SIZE 32 /* max rx frames in glom chain */

// The maximum number of VMOs that can be transferred in a single SDIO transaction
constexpr size_t kMaxVmosPerTransfer = 40;

static void brcmf_sdiod_ib_irqhandler(struct brcmf_sdio_dev* sdiodev) {
  BRCMF_DBG(INTR, "IB intr triggered");

  brcmf_sdio_isr(sdiodev->bus);
}

/* dummy handler for SDIO function 2 interrupt */
static void brcmf_sdiod_dummy_irqhandler(struct brcmf_sdio_dev* sdiodev) {}

zx_status_t brcmf_sdiod_configure_oob_interrupt(struct brcmf_sdio_dev* sdiodev,
                                                wifi_config_t* config) {
  zx_status_t ret = gpio_config_in(&sdiodev->gpios[WIFI_OOB_IRQ_GPIO_INDEX], GPIO_NO_PULL);
  if (ret != ZX_OK) {
    BRCMF_ERR("brcmf_sdiod_intr_register: gpio_config failed: %d", ret);
    return ret;
  }

  ret = gpio_get_interrupt(&sdiodev->gpios[WIFI_OOB_IRQ_GPIO_INDEX], config->oob_irq_mode,
                           &sdiodev->irq_handle);
  if (ret != ZX_OK) {
    BRCMF_ERR("brcmf_sdiod_intr_register: gpio_get_interrupt failed: %d", ret);
    return ret;
  }
  return ZX_OK;
}

zx_status_t brcmf_sdiod_get_bootloader_macaddr(struct brcmf_sdio_dev* sdiodev, uint8_t* macaddr) {
  // MAC address is only 6 bytes, but it is rounded up to 8 in the ZBI
  uint8_t bootloader_macaddr[8];
  size_t actual_len;
  zx_status_t ret = sdiodev->drvr->device->DeviceGetMetadata(
      DEVICE_METADATA_MAC_ADDRESS, bootloader_macaddr, sizeof(bootloader_macaddr), &actual_len);

  if (ret != ZX_OK || actual_len < ETH_ALEN) {
    return ret;
  }
  memcpy(macaddr, bootloader_macaddr, 6);
  BRCMF_DBG(SDIO, "got bootloader mac address");
#if !defined(NDEBUG)
  BRCMF_DBG(SDIO, "  address: " FMT_MAC, FMT_MAC_ARGS(macaddr));
#endif /* !defined(NDEBUG) */
  return ZX_OK;
}

zx_status_t brcmf_sdiod_intr_register(struct brcmf_sdio_dev* sdiodev) {
  struct brcmf_sdio_pd* pdata;
  zx_status_t ret = ZX_OK;
  uint8_t data;
  uint32_t addr, gpiocontrol;

  pdata = sdiodev->settings->bus.sdio;
  pdata->oob_irq_supported = false;
  wifi_config_t config;
  size_t actual;

  // Get Broadcom WiFi Metadata by calling the bus specific function
  if (sdiodev && sdiodev->bus_if && sdiodev->bus_if->ops) {
    ret = brcmf_bus_get_wifi_metadata(sdiodev->bus_if, &config, sizeof(wifi_config_t), &actual);
    if ((ret != ZX_OK && ret != ZX_ERR_NOT_FOUND) ||
        (ret == ZX_OK && actual != sizeof(wifi_config_t))) {
      BRCMF_ERR("brcmf_sdiod_intr_register: device_get_metadata failed");
      return ret;
    }
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // If there is metadata, OOB is supported.
  if (ret == ZX_OK) {
    BRCMF_DBG(SDIO, "Enter, register OOB IRQ");
    ret = brcmf_sdiod_configure_oob_interrupt(sdiodev, &config);
    if (ret != ZX_OK) {
      return ret;
    }
    pdata->oob_irq_supported = true;
    int status = thrd_create_with_name(&sdiodev->isr_thread, &brcmf_sdio_oob_irqhandler, sdiodev,
                                       "brcmf-sdio-isr");
    if (status != thrd_success) {
      BRCMF_ERR("thrd_create_with_name failed: %d", status);
      return ZX_ERR_INTERNAL;
    }
    sdiodev->oob_irq_requested = true;
    ret = enable_irq_wake(sdiodev->irq_handle);
    if (ret != ZX_OK) {
      BRCMF_ERR("enable_irq_wake failed %d", ret);
      return ret;
    }
    sdiodev->irq_wake = true;

    sdio_claim_host(sdiodev->func1);

    if (sdiodev->bus_if->chip == BRCM_CC_43362_CHIP_ID) {
      /* assign GPIO to SDIO core */
      addr = CORE_CC_REG(SI_ENUM_BASE, gpiocontrol);
      gpiocontrol = brcmf_sdiod_func1_rl(sdiodev, addr, &ret);
      gpiocontrol |= 0x2;
      brcmf_sdiod_func1_wl(sdiodev, addr, gpiocontrol, &ret);

      brcmf_sdiod_func1_wb(sdiodev, SBSDIO_GPIO_SELECT, 0xf, &ret);
      brcmf_sdiod_func1_wb(sdiodev, SBSDIO_GPIO_OUT, 0, &ret);
      brcmf_sdiod_func1_wb(sdiodev, SBSDIO_GPIO_EN, 0x2, &ret);
    }

    /* must configure SDIO_CCCR_INT_ENABLE to enable irq */
    sdio_enable_fn_intr(&sdiodev->sdio_proto_fn1);
    sdio_enable_fn_intr(&sdiodev->sdio_proto_fn2);

    /* redirect, configure and enable io for interrupt signal */
    data = SDIO_CCCR_BRCM_SEPINT_MASK | SDIO_CCCR_BRCM_SEPINT_OE;
    if (config.oob_irq_mode == ZX_INTERRUPT_MODE_LEVEL_HIGH) {
      data |= SDIO_CCCR_BRCM_SEPINT_ACT_HI;
    }
    brcmf_sdiod_vendor_control_wb(sdiodev, SDIO_CCCR_BRCM_SEPINT, data, &ret);
    // TODO(cphoenix): This pause is probably unnecessary.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    sdio_release_host(sdiodev->func1);
  } else {
    BRCMF_DBG(SDIO, "Entering");
    sdio_claim_host(sdiodev->func1);
    sdio_enable_fn_intr(&sdiodev->sdio_proto_fn1);
    (void)brcmf_sdiod_ib_irqhandler;  // TODO(cphoenix): If we use these, plug them in later.
    sdio_enable_fn_intr(&sdiodev->sdio_proto_fn2);
    (void)brcmf_sdiod_dummy_irqhandler;
    sdio_release_host(sdiodev->func1);
    sdiodev->sd_irq_requested = true;
  }

  return ZX_OK;
}

void brcmf_sdiod_intr_unregister(struct brcmf_sdio_dev* sdiodev) {
  BRCMF_DBG(SDIO, "Entering oob=%d sd=%d", sdiodev->oob_irq_requested, sdiodev->sd_irq_requested);

  if (sdiodev->oob_irq_requested) {
    sdio_claim_host(sdiodev->func1);
    brcmf_sdiod_vendor_control_wb(sdiodev, SDIO_CCCR_BRCM_SEPINT, 0, NULL);
    sdio_disable_fn_intr(&sdiodev->sdio_proto_fn1);
    sdio_disable_fn_intr(&sdiodev->sdio_proto_fn2);
    sdio_release_host(sdiodev->func1);

    sdiodev->oob_irq_requested = false;
    if (sdiodev->irq_wake) {
      disable_irq_wake(sdiodev->irq_handle);
      sdiodev->irq_wake = false;
    }
    zx_handle_close(sdiodev->irq_handle);
    int retval = 0;
    int status = thrd_join(sdiodev->isr_thread, &retval);
    if (status != thrd_success) {
      BRCMF_ERR("thrd_join failed: %d", status);
    }
    sdiodev->oob_irq_requested = false;
  }

  if (sdiodev->sd_irq_requested) {
    sdio_claim_host(sdiodev->func1);
    sdio_disable_fn_intr(&sdiodev->sdio_proto_fn2);
    sdio_disable_fn_intr(&sdiodev->sdio_proto_fn1);
    sdio_release_host(sdiodev->func1);
    sdiodev->sd_irq_requested = false;
  }
}

void brcmf_sdiod_change_state(struct brcmf_sdio_dev* sdiodev, enum brcmf_sdiod_state state) {
  if (sdiodev->state == BRCMF_SDIOD_NOMEDIUM || state == sdiodev->state) {
    BRCMF_ERR("No medium or equal state: %d", sdiodev->state);
    return;
  }

  BRCMF_DBG(TRACE, "%d -> %d", sdiodev->state, state);
  switch (sdiodev->state) {
    case BRCMF_SDIOD_DATA:
      /* any other state means bus interface is down */
      brcmf_bus_change_state(sdiodev->bus_if, BRCMF_BUS_DOWN);
      break;
    case BRCMF_SDIOD_DOWN:
      /* transition from DOWN to DATA means bus interface is up */
      if (state == BRCMF_SDIOD_DATA) {
        brcmf_bus_change_state(sdiodev->bus_if, BRCMF_BUS_UP);
      }
      break;
    default:
      break;
  }
  sdiodev->state = state;
}

static zx_status_t brcmf_sdiod_transfer_vmo(struct brcmf_sdio_dev* sdiodev,
                                            const sdio_protocol_t* proto, uint32_t addr, bool write,
                                            const wlan::drivers::components::Frame& frame,
                                            size_t size, bool fifo) {
  sdmmc_buffer_region_t buffer = {
      .buffer = {.vmo_id = frame.VmoId()},
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = frame.VmoOffset(),
      .size = size,
  };
  sdio_rw_txn_new_t txn = {
      .addr = addr,
      .incr = !fifo,
      .write = write,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  const uint32_t cache_op =
      write ? ZX_CACHE_FLUSH_DATA : (ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  zx_status_t err = zx_cache_flush(frame.Data(), size, cache_op);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to flush cache before SDIO transaction: %s", zx_status_get_string(err));
    return err;
  }

  TRACE_DURATION("brcmfmac:isr", "sdio_do_rw_txn_new");
  zx_status_t result = sdio_do_rw_txn_new(proto, &txn);
  if (result != ZX_OK) {
    BRCMF_ERR("SDIO transaction failed: %s", zx_status_get_string(result));
    return result;
  }

  if (!write) {
    result = zx_cache_flush(frame.Data(), size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    if (result != ZX_OK) {
      BRCMF_ERR("Failed to flush cache when reading from SDIO: %s", zx_status_get_string(result));
      return result;
    }
  }

  return ZX_OK;
}

template <typename It>
static zx_status_t brcmf_sdiod_transfer_vmos(struct brcmf_sdio_dev* sdiodev,
                                             const sdio_protocol_t* proto, uint32_t addr,
                                             bool write, It begin, It end, size_t count,
                                             bool fifo) {
  sdmmc_buffer_region_t buffers[kMaxVmosPerTransfer];

  size_t buffer = 0;
  for (auto frame = begin; frame != end; ++frame, ++buffer) {
    if (buffer >= std::size(buffers)) {
      BRCMF_ERR("Not enough buffers to send frames, sending %lu frames, only have %lu buffers",
                count, std::size(buffers));
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    buffers[buffer] = {
        .buffer = {.vmo_id = frame->VmoId()},
        .type = SDMMC_BUFFER_TYPE_VMO_ID,
        .offset = frame->VmoOffset(),
        .size = frame->Size(),
    };

    const uint32_t cache_op =
        write ? ZX_CACHE_FLUSH_DATA : (ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    zx_status_t result = zx_cache_flush(frame->Data(), frame->Size(), cache_op);
    if (result != ZX_OK) {
      BRCMF_ERR("Failed to flush cache when writing to SDIO: %s", zx_status_get_string(result));
      return result;
    }
  }

  sdio_rw_txn_new_t txn = {
      .addr = addr,
      .incr = !fifo,
      .write = write,
      .buffers_list = buffers,
      .buffers_count = buffer,
  };

  TRACE_DURATION("brcmfmac:isr", "sdio_do_rw_txn_new");
  zx_status_t result = sdio_do_rw_txn_new(proto, &txn);
  if (result != ZX_OK) {
    BRCMF_ERR("SDIO transaction failed: %s", zx_status_get_string(result));
  }

  if (!write) {
    for (auto frame = begin; frame != end; ++frame) {
      result = zx_cache_flush(frame->Data(), frame->Size(),
                              ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
      if (result != ZX_OK) {
        BRCMF_ERR("Failed to flush cache when reading from SDIO: %s", zx_status_get_string(result));
        return result;
      }
    }
  }

  return result;
}

zx_status_t brcmf_sdiod_read(brcmf_sdio_dev* sdiodev, const sdio_protocol_t* proto, uint32_t addr,
                             void* data, size_t size, bool fifo) {
  wlan::drivers::components::FrameContainer frames =
      brcmf_sdio_acquire_internal_rx_space_to_size(sdiodev->bus, size);
  if (frames.empty()) {
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status = brcmf_sdiod_transfer_vmos(sdiodev, proto, addr, false, frames.begin(),
                                                 frames.end(), frames.size(), fifo);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t remaining = size;
  size_t offset = 0;
  for (auto frame = frames.begin(); remaining > 0 && frame != frames.end(); ++frame) {
    uint32_t to_copy = std::min(remaining, frame->Size());
    memcpy(reinterpret_cast<uint8_t*>(data) + offset, frame->Data(), to_copy);
    remaining -= to_copy;
    offset += to_copy;
  }

  return ZX_OK;
}

zx_status_t brcmf_sdiod_write(brcmf_sdio_dev* sdiodev, const sdio_protocol_t* proto, uint32_t addr,
                              void* data, size_t size, bool fifo) {
  wlan::drivers::components::FrameContainer frames =
      brcmf_sdio_acquire_and_fill_tx_space(sdiodev->bus, reinterpret_cast<uint8_t*>(data), size);
  if (frames.empty()) {
    return ZX_ERR_NO_RESOURCES;
  }

  return brcmf_sdiod_transfer_vmos(sdiodev, proto, addr, true, frames.begin(), frames.end(),
                                   frames.size(), fifo);
}

uint8_t brcmf_sdiod_vendor_control_rb(struct brcmf_sdio_dev* sdiodev, uint8_t addr,
                                      zx_status_t* result_out) {
  uint8_t data = 0;
  // Any function device can access the vendor control registers; fn2 could be used here instead.
  zx_status_t result =
      sdio_do_vendor_control_rw_byte(&sdiodev->sdio_proto_fn1, false, addr, 0, &data);
  if (result_out) {
    *result_out = result;
  }
  return data;
}

uint8_t brcmf_sdiod_func1_rb(struct brcmf_sdio_dev* sdiodev, uint32_t addr,
                             zx_status_t* result_out) {
  uint8_t data;
  zx_status_t result = sdio_do_rw_byte(&sdiodev->sdio_proto_fn1, false, addr, 0, &data);
  if (result_out) {
    *result_out = result;
  }
  return data;
}

void brcmf_sdiod_vendor_control_wb(struct brcmf_sdio_dev* sdiodev, uint8_t addr, uint8_t value,
                                   zx_status_t* result_out) {
  // Any function device can access the vendor control registers; fn2 could be used here instead.
  zx_status_t result =
      sdio_do_vendor_control_rw_byte(&sdiodev->sdio_proto_fn1, true, addr, value, nullptr);
  if (result_out) {
    *result_out = result;
  }
}

void brcmf_sdiod_func1_wb(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint8_t value,
                          zx_status_t* result_out) {
  zx_status_t result = sdio_do_rw_byte(&sdiodev->sdio_proto_fn1, true, addr, value, nullptr);
  if (result_out) {
    *result_out = result;
  }
}

static zx_status_t brcmf_sdiod_set_backplane_window(struct brcmf_sdio_dev* sdiodev, uint32_t addr) {
  uint32_t v;
  uint32_t bar0 = addr & SBSDIO_SBWINDOW_MASK;
  zx_status_t err = ZX_OK;
  int i;

  if (bar0 == sdiodev->sbwad) {
    return ZX_OK;
  }

  v = bar0 >> 8;

  for (i = 0; i < 3 && err == ZX_OK; i++, v >>= 8) {
    brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_SBADDRLOW + i, v & 0xff, &err);
  }

  if (err == ZX_OK) {
    sdiodev->sbwad = bar0;
  }

  return err;
}

uint32_t brcmf_sdiod_func1_rl(struct brcmf_sdio_dev* sdiodev, uint32_t addr,
                              zx_status_t* result_out) {
  uint32_t data = 0;

  zx_status_t retval = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (retval == ZX_OK) {
    wlan::drivers::components::Frame& frame = sdiodev->bus->rx_tx_data.rx_frame;
    SBSDIO_FORMAT_ADDR(addr);
    retval = brcmf_sdiod_transfer_vmo(sdiodev, &sdiodev->sdio_proto_fn1, addr, false, frame,
                                      sizeof(data), false);
    if (retval == ZX_OK) {
      memcpy(&data, frame.Data(), sizeof(data));
    }
  }
  if (result_out) {
    *result_out = retval;
  }

  return data;
}

void brcmf_sdiod_func1_wl(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint32_t data,
                          zx_status_t* result_out) {
  zx_status_t retval = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (retval == ZX_OK) {
    wlan::drivers::components::Frame& frame = sdiodev->bus->rx_tx_data.tx_frame;
    memcpy(frame.Data(), &data, sizeof(data));
    SBSDIO_FORMAT_ADDR(addr);
    retval = brcmf_sdiod_transfer_vmo(sdiodev, &sdiodev->sdio_proto_fn1, addr, true, frame,
                                      sizeof(addr), false);
  }
  if (result_out) {
    *result_out = retval;
  }
}

static zx_status_t brcmf_sdiod_netbuf_read(struct brcmf_sdio_dev* sdiodev,
                                           const sdio_protocol_t* proto, uint32_t addr,
                                           uint8_t* data, size_t size, bool fifo) {
  TRACE_DURATION("brcmfmac:isr", "netbuf_read", "len", size);

  SBSDIO_FORMAT_ADDR(addr);
  /* Single netbuf use the standard mmc interface */
  if ((size & 3u) != 0) {
    BRCMF_ERR("Unaligned SDIO read size %zu", size);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t err = brcmf_sdiod_read(sdiodev, proto, addr, data, size, fifo);
  if (err == ZX_ERR_IO_REFUSED) {
    brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
  }

  return err;
}

static zx_status_t brcmf_sdiod_netbuf_write(struct brcmf_sdio_dev* sdiodev,
                                            const sdio_protocol_t* proto, uint32_t addr,
                                            uint8_t* data, size_t size) {
  TRACE_DURATION("brcmfmac:isr", "sdiod_netbuf_write", "len", size);

  SBSDIO_FORMAT_ADDR(addr);
  /* Single netbuf use the standard mmc interface */
  if ((size & 3u) != 0) {
    BRCMF_ERR("Unaligned SDIO write size %zu", size);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t err = brcmf_sdiod_write(sdiodev, proto, addr, data, size, false);

  if (err == ZX_ERR_IO_REFUSED) {
    brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
  }

  return err;
}

zx_status_t brcmf_sdiod_recv_frame(struct brcmf_sdio_dev* sdiodev,
                                   wlan::drivers::components::Frame& frame, uint32_t bytes) {
  uint32_t addr = sdiodev->cc_core->base;

  zx_status_t err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (err != ZX_OK) {
    return err;
  }

  SBSDIO_FORMAT_ADDR(addr);

  return brcmf_sdiod_transfer_vmo(sdiodev, &sdiodev->sdio_proto_fn2, addr, false, frame, bytes,
                                  false);
}

zx_status_t brcmf_sdiod_recv_frames(struct brcmf_sdio_dev* sdiodev,
                                    cpp20::span<wlan::drivers::components::Frame> frames) {
  uint32_t addr = sdiodev->cc_core->base;
  zx_status_t err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to set backplace window: %s", zx_status_get_string(err));
    return err;
  }

  SBSDIO_FORMAT_ADDR(addr);

  return brcmf_sdiod_transfer_vmos(sdiodev, &sdiodev->sdio_proto_fn2, addr, false, frames.begin(),
                                   frames.end(), frames.size(), false);
}

zx_status_t brcmf_sdiod_send_buf(struct brcmf_sdio_dev* sdiodev, uint8_t* buf, uint nbytes) {
  uint32_t addr = sdiodev->cc_core->base;
  zx_status_t err;
  uint32_t req_sz;

  TRACE_DURATION("brcmfmac:isr", "sdiod_send_buf");

  err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (err != ZX_OK) {
    return err;
  }

  req_sz = ZX_ROUNDUP(nbytes, SDIOD_SIZE_ALIGNMENT);

  return brcmf_sdiod_netbuf_write(sdiodev, &sdiodev->sdio_proto_fn2, addr, buf, req_sz);
}

zx_status_t brcmf_sdiod_send_frames(struct brcmf_sdio_dev* sdiodev,
                                    cpp20::span<wlan::drivers::components::Frame>& frames,
                                    uint32_t* frames_sent_out) {
  uint32_t addr = sdiodev->cc_core->base;
  *frames_sent_out = 0;

  TRACE_DURATION("brcmfmac:isr", "sdiod_send_frames");

  zx_status_t err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (err != ZX_OK) {
    return err;
  }

  if (sdiodev->txglom && frames.size() > 1) {
    err = brcmf_sdiod_transfer_vmos(sdiodev, &sdiodev->sdio_proto_fn2, addr, true, frames.begin(),
                                    frames.end(), frames.size(), false);
    if (err != ZX_OK) {
      if (err == ZX_ERR_IO_REFUSED) {
        brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
      }
      return err;
    }
    (*frames_sent_out) += frames.size();
    return ZX_OK;
  }
  for (auto& frame : frames) {
    err = brcmf_sdiod_transfer_vmo(sdiodev, &sdiodev->sdio_proto_fn2, addr, true, frame,
                                   frame.Size(), false);
    if (err != ZX_OK) {
      if (err == ZX_ERR_IO_REFUSED) {
        brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
      }
      return err;
    }
    ++(*frames_sent_out);
  }
  return ZX_OK;
}

zx_status_t brcmf_sdiod_ramrw(struct brcmf_sdio_dev* sdiodev, bool write, uint32_t address,
                              void* data, size_t data_size) {
  zx_status_t err = ZX_OK;
  // SBSDIO_SB_OFT_ADDR_LIMIT is the max transfer limit a single chunk.
  uint32_t transfer_address = address & SBSDIO_SB_OFT_ADDR_MASK;
  size_t transfer_size;

  /* Determine initial transfer parameters */
  sdio_claim_host(sdiodev->func1);
  // Handling the situation when transfer_address + transfer_size > SBSDIO_SB_OFT_ADDR_LIMIT by
  // doing address alignment.
  if ((transfer_address + data_size) & SBSDIO_SBWINDOW_MASK) {
    transfer_size = (SBSDIO_SB_OFT_ADDR_LIMIT - transfer_address);
  } else {
    transfer_size = data_size;
  }

  /* Do the transfer(s) */
  while (data_size) {
    /* Set the backplane window to include the start address */
    err = brcmf_sdiod_set_backplane_window(sdiodev, address);

    if (err != ZX_OK) {
      break;
    }

    if (write) {
      err = brcmf_sdiod_netbuf_write(sdiodev, &sdiodev->sdio_proto_fn1, transfer_address,
                                     reinterpret_cast<uint8_t*>(data), transfer_size);
    } else {
      err = brcmf_sdiod_netbuf_read(sdiodev, &sdiodev->sdio_proto_fn1, transfer_address,
                                    reinterpret_cast<uint8_t*>(data), transfer_size, false);
    }

    if (err != ZX_OK) {
      BRCMF_ERR("membytes transfer failed: %s", zx_status_get_string(err));
      break;
    }

    /* Adjust for next transfer (if any) */
    data_size -= transfer_size;
    if (data)
      data = static_cast<char*>(data) + transfer_size;
    address += transfer_size;
    transfer_address += transfer_size;
    transfer_size = std::min<size_t>(SBSDIO_SB_OFT_ADDR_LIMIT, data_size);
  }

  sdio_release_host(sdiodev->func1);

  return err;
}

zx_status_t brcmf_sdiod_abort(struct brcmf_sdio_dev* sdiodev, uint32_t func) {
  BRCMF_DBG(SDIO, "Enter");

  /* Issue abort cmd52 command through F0 */
  if (func == SDIO_FN_1) {
    sdio_io_abort(&sdiodev->sdio_proto_fn1);
  } else {
    sdio_io_abort(&sdiodev->sdio_proto_fn2);
  }

  BRCMF_DBG(SDIO, "Exit");
  return ZX_OK;
}

static zx_status_t brcmf_sdiod_remove(struct brcmf_sdio_dev* sdiodev) {
  sdiodev->state = BRCMF_SDIOD_DOWN;
  if (sdiodev->bus) {
    brcmf_sdio_remove(sdiodev->bus);
    sdiodev->bus = NULL;
  }

  /* Disable Function 2 */
  sdio_claim_host(sdiodev->func2);
  sdio_disable_fn(&sdiodev->sdio_proto_fn2);
  sdio_release_host(sdiodev->func2);

  /* Disable Function 1 */
  sdio_claim_host(sdiodev->func1);
  sdio_disable_fn(&sdiodev->sdio_proto_fn1);
  sdio_release_host(sdiodev->func1);

  sdiodev->sbwad = 0;

  // TODO(cphoenix): Power management stuff
  // pm_runtime_allow(sdiodev->func1->card->host->parent);
  return ZX_OK;
}

// TODO(cphoenix): Power management stuff
#ifdef POWER_MANAGEMENT
static void brcmf_sdiod_host_fixup(struct mmc_host* host) {
  /* runtime-pm powers off the device */
  pm_runtime_forbid(host->parent);
  /* avoid removal detection upon resume */
  host->caps |= MMC_CAP_NONREMOVABLE;  // Defined outside this driver's codebase
}
#endif  // POWER_MANAGEMENT

static zx_status_t brcmf_sdiod_set_block_size(sdio_protocol_t* proto, sdio_func* func,
                                              uint16_t block_size) {
  zx_status_t ret = sdio_update_block_size(proto, block_size, false);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to update block size: %s", zx_status_get_string(ret));
    return ret;
  }
  // Cache the block size so we don't have to request it every time we do alignment calculations.
  ret = sdio_get_block_size(proto, &func->blocksize);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to get block size: %s", zx_status_get_string(ret));
    return ret;
  }
  ZX_ASSERT(func->blocksize == block_size);
  return ZX_OK;
}

static zx_status_t brcmf_sdiod_probe(struct brcmf_sdio_dev* sdiodev) {
  zx_status_t ret = ZX_OK;

  ret = brcmf_sdiod_set_block_size(&sdiodev->sdio_proto_fn1, sdiodev->func1, SDIO_FUNC1_BLOCKSIZE);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to set F1 blocksize: %s", zx_status_get_string(ret));
    goto out;
  }
  ret = brcmf_sdiod_set_block_size(&sdiodev->sdio_proto_fn2, sdiodev->func2, SDIO_FUNC2_BLOCKSIZE);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to set F2 blocksize: %s", zx_status_get_string(ret));
    goto out;
  }

  /* increase F2 timeout */
  // TODO(cphoenix): SDIO doesn't use timeout yet
  // sdiodev->func2->enable_timeout = SDIO_WAIT_F2RDY;

  /* Enable Function 1 */
  ret = sdio_enable_fn(&sdiodev->sdio_proto_fn1);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to enable F1: err=%d", ret);
    goto out;
  }

  /* try to attach to the target device */
  sdiodev->bus = brcmf_sdio_probe(sdiodev);
  if (!sdiodev->bus) {
    ret = ZX_ERR_IO_NOT_PRESENT;
    goto out;
  }
  // brcmf_sdiod_host_fixup(sdiodev->func2->card->host);
out:
  if (ret != ZX_OK) {
    brcmf_sdiod_remove(sdiodev);
  }

  return ret;
}

#ifdef TODO_ADD_SDIO_IDS  // Put some of these into binding.c
#define BRCMF_SDIO_DEVICE(dev_id) \
  { SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, dev_id) }

/* devices we support, null terminated */
static const struct sdio_device_id brcmf_sdmmc_ids[] = {
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43143),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43241),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4329),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4330),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4334),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43340),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43341),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43362),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4335_4339),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4339),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43430),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4345),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43455),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4354),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4356),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_CYPRESS_4373),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4359),
    {/* end: all zeroes */}};
#endif  // TODO_ADD_SDIO_IDS

zx_status_t brcmf_sdio_register(brcmf_pub* drvr, std::unique_ptr<brcmf_bus>* out_bus) {
  zx_status_t err;

  std::unique_ptr<struct brcmf_bus> bus_if;
  struct sdio_func* func1 = NULL;
  struct sdio_func* func2 = NULL;
  struct brcmf_sdio_dev* sdiodev = NULL;

  BRCMF_DBG(SDIO, "Enter");
  // One for SDIO, one or two GPIOs.
  sdio_protocol_t sdio_proto_fn1;
  sdio_protocol_t sdio_proto_fn2;
  gpio_protocol_t gpio_protos[GPIO_COUNT];
  bool has_debug_gpio = false;

  ddk::SdioProtocolClient sdio_fn1(drvr->device->parent(), "sdio-function-1");
  if (!sdio_fn1.is_valid()) {
    BRCMF_ERR("sdio function 1 fragment not found");
    return ZX_ERR_NO_RESOURCES;
  }
  sdio_fn1.GetProto(&sdio_proto_fn1);

  ddk::SdioProtocolClient sdio_fn2(drvr->device->parent(), "sdio-function-2");
  if (!sdio_fn2.is_valid()) {
    BRCMF_ERR("sdio function 2 fragment not found");
    return ZX_ERR_NO_RESOURCES;
  }
  sdio_fn2.GetProto(&sdio_proto_fn2);

  ddk::GpioProtocolClient gpio(drvr->device->parent(), "gpio-oob");
  if (!gpio.is_valid()) {
    BRCMF_ERR("ZX_PROTOCOL_GPIO not found");
    return ZX_ERR_NO_RESOURCES;
  }
  gpio.GetProto(&gpio_protos[WIFI_OOB_IRQ_GPIO_INDEX]);

  // Debug GPIO is optional
  gpio = ddk::GpioProtocolClient(drvr->device->parent(), "gpio-debug");
  if (gpio.is_valid()) {
    has_debug_gpio = true;
    gpio.GetProto(&gpio_protos[DEBUG_GPIO_INDEX]);
  }

  sdio_hw_info_t devinfo;
  sdio_get_dev_hw_info(&sdio_proto_fn1, &devinfo);
  if (devinfo.dev_hw_info.num_funcs < 3) {
    BRCMF_ERR("Not enough SDIO funcs (need 3, have %d)", devinfo.dev_hw_info.num_funcs);
    return ZX_ERR_IO;
  }

  pthread_mutexattr_t mutex_attr;
  if (pthread_mutexattr_init(&mutex_attr)) {
    return ZX_ERR_INTERNAL;
  }
  if (pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE)) {
    err = ZX_ERR_NO_MEMORY;
    goto fail;
  }

  BRCMF_DBG(SDIO, "sdio vendor ID: 0x%04x", devinfo.funcs_hw_info[SDIO_FN_1].manufacturer_id);
  BRCMF_DBG(SDIO, "sdio device ID: 0x%04x", devinfo.funcs_hw_info[SDIO_FN_1].product_id);

  // TODO(cphoenix): Reexamine this when SDIO is more mature - do we need to support "quirks" in
  // Fuchsia? (MMC_QUIRK_LENIENT_FN0 is defined outside this driver.)
  /* Set MMC_QUIRK_LENIENT_FN0 for this card */
  // func->card->quirks |= MMC_QUIRK_LENIENT_FN0;

  bus_if = std::make_unique<brcmf_bus>();
  func1 = static_cast<decltype(func1)>(calloc(1, sizeof(struct sdio_func)));
  if (!func1) {
    err = ZX_ERR_NO_MEMORY;
    goto fail;
  } else {
    if (pthread_mutex_init(&func1->lock, &mutex_attr)) {
      free(func1);
      func1 = NULL;
      err = ZX_ERR_INTERNAL;
      goto fail;
    }
  }
  func2 = static_cast<decltype(func2)>(calloc(1, sizeof(struct sdio_func)));
  if (!func2) {
    err = ZX_ERR_NO_MEMORY;
    goto fail;
  } else {
    if (pthread_mutex_init(&func2->lock, &mutex_attr)) {
      free(func2);
      func2 = NULL;
      err = ZX_ERR_INTERNAL;
      goto fail;
    }
  }
  sdiodev = new brcmf_sdio_dev{};
  if (!sdiodev) {
    err = ZX_ERR_NO_MEMORY;
    goto fail;
  }
  memcpy(&sdiodev->sdio_proto_fn1, &sdio_proto_fn1, sizeof(sdiodev->sdio_proto_fn1));
  memcpy(&sdiodev->sdio_proto_fn2, &sdio_proto_fn2, sizeof(sdiodev->sdio_proto_fn2));
  memcpy(&sdiodev->gpios[WIFI_OOB_IRQ_GPIO_INDEX], &gpio_protos[WIFI_OOB_IRQ_GPIO_INDEX],
         sizeof(gpio_protos[WIFI_OOB_IRQ_GPIO_INDEX]));
  if (has_debug_gpio) {
    memcpy(&sdiodev->gpios[DEBUG_GPIO_INDEX], &gpio_protos[DEBUG_GPIO_INDEX],
           sizeof(gpio_protos[DEBUG_GPIO_INDEX]));
    sdiodev->has_debug_gpio = true;
  }
  sdiodev->bus_if = bus_if.get();
  sdiodev->func1 = func1;
  sdiodev->func2 = func2;
  sdiodev->ctl_done_timeout = ZX_MSEC(CTL_DONE_TIMEOUT_MSEC);
  sdiodev->drvr = drvr;
  bus_if->bus_priv.sdio = sdiodev;

  sdiodev->manufacturer_id = devinfo.funcs_hw_info[SDIO_FN_1].manufacturer_id;
  sdiodev->product_id = devinfo.funcs_hw_info[SDIO_FN_1].product_id;

  // No need to call brcmf_sdiod_change_state here. Since the bus struct was allocated above it
  // can't contain any meaningful previous state that we can transition from. So we just need to set
  // the expected bus state here. This way we avoid any spurious errors messages about setting the
  // state to the same value if they happen to match.
  sdiodev->state = BRCMF_SDIOD_DOWN;

  BRCMF_DBG(SDIO, "F2 found, calling brcmf_sdiod_probe...");
  err = brcmf_sdiod_probe(sdiodev);
  if (err != ZX_OK) {
    BRCMF_ERR("F2 error, probe failed %d...", err);
    goto fail;
  }

  pthread_mutexattr_destroy(&mutex_attr);
  BRCMF_DBG(SDIO, "F2 init completed...");

  *out_bus = std::move(bus_if);
  return ZX_OK;

fail:
  delete sdiodev;
  if (func2) {
    pthread_mutex_destroy(&func2->lock);
    free(func2);
  }
  if (func1) {
    pthread_mutex_destroy(&func1->lock);
    free(func1);
  }
  pthread_mutexattr_destroy(&mutex_attr);
  return err;
}

static void brcmf_ops_sdio_remove(struct brcmf_sdio_dev* sdiodev) {
  BRCMF_DBG(SDIO, "Enter");
  if (sdiodev == NULL) {
    return;
  }
  BRCMF_DBG(SDIO, "sdio vendor ID: 0x%04x", sdiodev->manufacturer_id);
  BRCMF_DBG(SDIO, "sdio device ID: 0x%04x", sdiodev->product_id);

  /* start by unregistering irqs */
  brcmf_sdiod_intr_unregister(sdiodev);

  brcmf_sdiod_remove(sdiodev);

  if (sdiodev->func1) {
    pthread_mutex_destroy(&sdiodev->func1->lock);
    free(sdiodev->func1);
  }
  if (sdiodev->func2) {
    pthread_mutex_destroy(&sdiodev->func2->lock);
    free(sdiodev->func2);
  }

  BRCMF_DBG(SDIO, "Exit");
}

void brcmf_sdio_exit(brcmf_bus* bus) {
  BRCMF_DBG(SDIO, "Enter");

  brcmf_ops_sdio_remove(bus->bus_priv.sdio);
  delete bus->bus_priv.sdio;
  bus->bus_priv.sdio = nullptr;
}
