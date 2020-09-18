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

#include <inttypes.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/vmo.h>
#include <pthread.h>
#include <zircon/status.h>

#include <algorithm>
#include <atomic>

#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/sdio.h>
#include <ddk/trace/event.h>
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
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/linuxisms.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/macros.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio/sdio.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/soc.h"

#define SDIOH_API_ACCESS_RETRY_LIMIT 2

#define DMA_ALIGN_MASK 0x03

#define SDIO_FUNC1_BLOCKSIZE 64
#define SDIO_FUNC2_BLOCKSIZE 512
/* Maximum milliseconds to wait for F2 to come up */
#define SDIO_WAIT_F2RDY 3000

#define BRCMF_DEFAULT_RXGLOM_SIZE 32 /* max rx frames in glom chain */

// The initial size of the DMA buffer
constexpr size_t kDmaInitialBufferSize = 4096;
// The maximum size the DMA buffer should be allowed to grow to
constexpr size_t kDmaMaxBufferSize = 131072;
// The minimum size a packet should be to enable DMA
constexpr size_t kDmaThresholdSize = 64;

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
  zx_status_t ret =
      device_get_metadata(sdiodev->drvr->zxdev, DEVICE_METADATA_MAC_ADDRESS, bootloader_macaddr,
                          sizeof(bootloader_macaddr), &actual_len);

  if (ret != ZX_OK || actual_len < ETH_ALEN) {
    return ret;
  }
  memcpy(macaddr, bootloader_macaddr, 6);
  BRCMF_DBG(INFO, "got bootloader mac address: " MAC_FMT_STR, MAC_FMT_ARGS(macaddr));
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
    status = thrd_detach(sdiodev->isr_thread);
    if (status != thrd_success) {
      BRCMF_ERR("thrd_detach failed: %d", status);
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

zx_status_t brcmf_sdiod_transfer(struct brcmf_sdio_dev* sdiodev, uint8_t func, uint32_t addr,
                                 bool write, void* data, size_t size, bool fifo) {
  sdio_rw_txn_t txn;
  zx_status_t result;
  // The size must be a multiple of 4 to use DMA, also don't use DMA for very
  // small transfers as the overhead might not be worth it.
  bool use_dma = ((size % 4) == 0) && size > kDmaThresholdSize;

  TRACE_DURATION("brcmfmac:isr", "sdiod_transfer", "func", TA_UINT32((uint32_t)func), "type",
                 TA_STRING(write ? "write" : "read"), "addr", TA_UINT32(addr), "size",
                 TA_UINT64((uint64_t)size));
  if (use_dma) {
    if (size > sdiodev->dma_buffer_size) {
      // Only resize the DMA buffer if it's not big enough. This saves a
      // significant amount of time by not resizing the buffer for every
      // transfer. Use a cached size value to avoid a syscall each time.
      if (size > kDmaMaxBufferSize) {
        BRCMF_ERR("Requested SDIO transfer VMO size %" PRIu64 " too large, max is %" PRIu64 "",
                  size, kDmaMaxBufferSize);
        return ZX_ERR_NO_MEMORY;
      }
      result = sdiodev->dma_buffer.set_size(size);
      if (result != ZX_OK) {
        BRCMF_ERR("Error resizing SDIO transfer VMO: %s", zx_status_get_string(result));
        return result;
      }
      sdiodev->dma_buffer_size = size;
    }
    if (write) {
      result = sdiodev->dma_buffer.write(data, 0, size);
      if (result != ZX_OK) {
        BRCMF_ERR("Error writing to SDIO transfer VMO: %s", zx_status_get_string(result));
        return result;
      }
    }
  }

  txn.addr = addr;
  txn.write = write;
  txn.virt_buffer = data;
  txn.data_size = size;
  txn.incr = !fifo;
  txn.use_dma = use_dma;
  txn.dma_vmo = use_dma ? sdiodev->dma_buffer.get() : ZX_HANDLE_INVALID;
  txn.buf_offset = 0;

  if (func == SDIO_FN_1) {
    result = sdio_do_rw_txn(&sdiodev->sdio_proto_fn1, &txn);
  } else {
    result = sdio_do_rw_txn(&sdiodev->sdio_proto_fn2, &txn);
  }

  if (result != ZX_OK) {
    BRCMF_DBG(TEMP, "SDIO transaction failed: %s", zx_status_get_string(result));
  } else if (use_dma && !write) {
    // This is a read operation, read the data from the VMO to the buffer
    result = sdiodev->dma_buffer.read(data, 0, size);
    if (result != ZX_OK) {
      BRCMF_ERR("Error reading from SDIO transfer VMO: %s", zx_status_get_string(result));
      return result;
    }
  }
  return result;
}

static uint8_t brcmf_sdiod_func_rb(struct brcmf_sdio_dev* sdiodev, uint8_t func, uint32_t addr,
                                   zx_status_t* result_out) {
  uint8_t data;
  zx_status_t result;
  result = brcmf_sdiod_transfer(sdiodev, func, addr, false, &data, sizeof(data), false);
  if (result_out != NULL) {
    *result_out = result;
  }
  return data;
}

uint8_t brcmf_sdiod_vendor_control_rb(struct brcmf_sdio_dev* sdiodev, uint32_t addr,
                                      zx_status_t* result_out) {
  uint8_t data = 0;
  zx_status_t result;
  // Any function device can access the vendor control registers; fn2 could be used here instead.
  result = sdio_do_vendor_control_rw_byte(&sdiodev->sdio_proto_fn1, false, addr, 0, &data);
  if (result_out != NULL) {
    *result_out = result;
  }
  return data;
}

uint8_t brcmf_sdiod_func1_rb(struct brcmf_sdio_dev* sdiodev, uint32_t addr,
                             zx_status_t* result_out) {
  return brcmf_sdiod_func_rb(sdiodev, SDIO_FN_1, addr, result_out);
}

void brcmf_sdiod_vendor_control_wb(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint8_t data,
                                   zx_status_t* result_out) {
  zx_status_t result;
  // Any function device can access the vendor control registers; fn2 could be used here instead.
  result = sdio_do_vendor_control_rw_byte(&sdiodev->sdio_proto_fn1, true, addr, data, NULL);
  if (result_out != NULL) {
    *result_out = result;
  }
}

void brcmf_sdiod_func1_wb(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint8_t data,
                          zx_status_t* result_out) {
  zx_status_t result;
  result = brcmf_sdiod_transfer(sdiodev, SDIO_FN_1, addr, true, &data, sizeof(data), false);
  if (result_out != NULL) {
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

uint32_t brcmf_sdiod_func1_rl(struct brcmf_sdio_dev* sdiodev, uint32_t addr, zx_status_t* ret) {
  uint32_t data = 0;
  zx_status_t retval;

  retval = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (retval == ZX_OK) {
    SBSDIO_FORMAT_ADDR(addr);
    retval = brcmf_sdiod_transfer(sdiodev, SDIO_FN_1, addr, false, &data, sizeof(data), false);
  }
  if (ret) {
    *ret = retval;
  }

  return data;
}

void brcmf_sdiod_func1_wl(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint32_t data,
                          zx_status_t* ret) {
  zx_status_t retval;

  retval = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (retval == ZX_OK) {
    SBSDIO_FORMAT_ADDR(addr);
    retval = brcmf_sdiod_transfer(sdiodev, SDIO_FN_1, addr, true, &data, sizeof(data), false);
  }
  if (ret) {
    *ret = retval;
  }
}

static zx_status_t brcmf_sdiod_netbuf_read(struct brcmf_sdio_dev* sdiodev, uint32_t func,
                                           uint32_t addr, uint8_t* data, size_t size) {
  zx_status_t err;
  TRACE_DURATION("brcmfmac:isr", "netbuf_read", "func", TA_UINT32((uint32_t)func), "len",
                 TA_UINT32(size));

  SBSDIO_FORMAT_ADDR(addr);
  /* Single netbuf use the standard mmc interface */
  if ((size & 3u) != 0) {
    BRCMF_ERR("Unaligned SDIO read size %zu", size);
    return ZX_ERR_INVALID_ARGS;
  }

  switch (func) {
    case SDIO_FN_1:
      err = brcmf_sdiod_transfer(sdiodev, func, addr, false, data, size, false);
      break;
    case SDIO_FN_2:
      err = brcmf_sdiod_transfer(sdiodev, func, addr, false, data, size, true);
      break;
    default:
      /* bail out as things are really fishy here */
      WARN(1, "invalid sdio function number %d");
      err = ZX_ERR_IO_REFUSED;
  };

  if (err == ZX_ERR_IO_REFUSED) {
    brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
  }

  return err;
}

static zx_status_t brcmf_sdiod_netbuf_write(struct brcmf_sdio_dev* sdiodev, uint32_t func,
                                            uint32_t addr, uint8_t* data, size_t size) {
  zx_status_t err;

  TRACE_DURATION("brcmfmac:isr", "sdiod_netbuf_write", "func", TA_UINT32((uint32_t)func), "len",
                 TA_UINT32(size));

  SBSDIO_FORMAT_ADDR(addr);
  /* Single netbuf use the standard mmc interface */
  if ((size & 3u) != 0) {
    BRCMF_ERR("Unaligned SDIO write size %zu", size);
    return ZX_ERR_INVALID_ARGS;
  }

  err = brcmf_sdiod_transfer(sdiodev, func, addr, true, data, size, false);

  if (err == ZX_ERR_IO_REFUSED) {
    brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
  }

  return err;
}

zx_status_t brcmf_sdiod_recv_buf(struct brcmf_sdio_dev* sdiodev, uint8_t* buf, uint nbytes) {
  uint32_t addr = sdiodev->cc_core->base;
  zx_status_t err = ZX_OK;
  unsigned int req_sz;

  err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (err != ZX_OK) {
    return err;
  }

  SBSDIO_FORMAT_ADDR(addr);

  req_sz = ZX_ROUNDUP(nbytes, SDIOD_SIZE_ALIGNMENT);

  err = brcmf_sdiod_transfer(sdiodev, SDIO_FN_2, addr, false, buf, req_sz, true);

  return err;
}

zx_status_t brcmf_sdiod_recv_pkt(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf* pkt) {
  uint32_t addr = sdiodev->cc_core->base;
  zx_status_t err = ZX_OK;

  TRACE_DURATION("brcmfmac:isr", "recv_pkt");

  err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (err != ZX_OK) {
    goto done;
  }

  err = brcmf_sdiod_netbuf_read(sdiodev, SDIO_FN_2, addr, pkt->data, pkt->len);

done:
  return err;
}

zx_status_t brcmf_sdiod_recv_chain(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf_list* pktq,
                                   uint totlen) {
  struct brcmf_netbuf* glom_netbuf = NULL;
  struct brcmf_netbuf* netbuf;
  uint32_t addr = sdiodev->cc_core->base;
  zx_status_t err = ZX_OK;
  uint32_t list_len = brcmf_netbuf_list_length(pktq);

  TRACE_DURATION("brcmfmac:isr", "sdiod_recv_chain", "list_len", TA_UINT32(list_len));

  BRCMF_DBG(SDIO, "addr = 0x%x, size = %d", addr, list_len);

  err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (err != ZX_OK) {
    goto done;
  }

  if (list_len == 1) {
    netbuf = brcmf_netbuf_list_peek_head(pktq);
    err = brcmf_sdiod_netbuf_read(sdiodev, SDIO_FN_2, addr, netbuf->data, netbuf->len);
  } else {
    glom_netbuf = brcmu_pkt_buf_get_netbuf(totlen);
    if (!glom_netbuf) {
      return ZX_ERR_NO_MEMORY;
    }
    err = brcmf_sdiod_netbuf_read(sdiodev, SDIO_FN_2, addr, glom_netbuf->data, glom_netbuf->len);
    if (err != ZX_OK) {
      goto done;
    }

    brcmf_netbuf_list_for_every(pktq, netbuf) {
      memcpy(netbuf->data, glom_netbuf->data, netbuf->len);
      brcmf_netbuf_shrink_head(glom_netbuf, netbuf->len);
    }
  }

done:
  brcmu_pkt_buf_free_netbuf(glom_netbuf);
  return err;
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

  SBSDIO_FORMAT_ADDR(addr);

  req_sz = ZX_ROUNDUP(nbytes, SDIOD_SIZE_ALIGNMENT);

  if (err == ZX_OK) {
    err = brcmf_sdiod_transfer(sdiodev, SDIO_FN_2, addr, true, buf, req_sz, false);
  }

  if (err == ZX_ERR_IO_REFUSED) {
    brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
  }

  return err;
}

zx_status_t brcmf_sdiod_send_pkt(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf_list* pktq) {
  struct brcmf_netbuf* netbuf;
  uint32_t addr = sdiodev->cc_core->base;
  zx_status_t err;

  BRCMF_DBG(SDIO, "addr = 0x%x, size = %d", addr, brcmf_netbuf_list_length(pktq));

  TRACE_DURATION("brcmfmac:isr", "sdiod_send_pkt");

  err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
  if (err != ZX_OK) {
    return err;
  }

  brcmf_netbuf_list_for_every(pktq, netbuf) {
    // We use allocated_size minus head space here to take place of alignment of data size in
    // netbuf.
    err = brcmf_sdiod_netbuf_write(sdiodev, SDIO_FN_2, addr, netbuf->data,
                                   netbuf->allocated_size - brcmf_netbuf_head_space(netbuf));
    if (err != ZX_OK) {
      break;
    }
  }

  return err;
}

zx_status_t brcmf_sdiod_ramrw(struct brcmf_sdio_dev* sdiodev, bool write, uint32_t address,
                              void* data, size_t data_size) {
  zx_status_t err = ZX_OK;
  // SBSDIO_SB_OFT_ADDR_LIMIT is the max transfer limit a single chunk.
  uint32_t transfer_address = address & SBSDIO_SB_OFT_ADDR_MASK;
  uint32_t transfer_size;

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
      err = brcmf_sdiod_netbuf_write(sdiodev, SDIO_FN_1, transfer_address, (uint8_t*)data,
                                     transfer_size);
    } else {
      err = brcmf_sdiod_netbuf_read(sdiodev, SDIO_FN_1, transfer_address, (uint8_t*)data,
                                    transfer_size);
    }

    if (err != ZX_OK) {
      BRCMF_ERR("membytes transfer failed");
      break;
    }

    /* Adjust for next transfer (if any) */
    data_size -= transfer_size;
    if (data)
      data = static_cast<char*>(data) + transfer_size;
    address += transfer_size;
    transfer_address += transfer_size;
    transfer_size = std::min<uint>(SBSDIO_SB_OFT_ADDR_LIMIT, data_size);
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

static zx_status_t brcmf_sdiod_probe(struct brcmf_sdio_dev* sdiodev) {
  zx_status_t ret = ZX_OK;

  ret = sdio_update_block_size(&sdiodev->sdio_proto_fn1, SDIO_FUNC1_BLOCKSIZE, false);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to set F1 blocksize");
    goto out;
  }
  ret = sdio_update_block_size(&sdiodev->sdio_proto_fn2, SDIO_FUNC2_BLOCKSIZE, false);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to set F2 blocksize");
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
  zx_status_t status;

  std::unique_ptr<struct brcmf_bus> bus_if;
  struct sdio_func* func1 = NULL;
  struct sdio_func* func2 = NULL;
  struct brcmf_sdio_dev* sdiodev = NULL;

  BRCMF_DBG(SDIO, "Enter");

  composite_protocol_t composite_proto = {};
  status = device_get_protocol(drvr->zxdev, ZX_PROTOCOL_COMPOSITE, &composite_proto);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t fragment_count = composite_get_fragment_count(&composite_proto);
  if (fragment_count < 2) {
    BRCMF_ERR("Not enough fragments (need atleast 2, have %u)", fragment_count);
    return ZX_ERR_INTERNAL;
  }
  // One for SDIO, one or two GPIOs.
  zx_device_t* fragments[FRAGMENT_COUNT];
  size_t actual;
  composite_get_fragments(&composite_proto, fragments, countof(fragments), &actual);
  if (actual < 2) {
    BRCMF_ERR("Not enough fragments (need atleast 2, have %zu)", actual);
    return ZX_ERR_INTERNAL;
  }

  sdio_protocol_t sdio_proto_fn1;
  sdio_protocol_t sdio_proto_fn2;
  gpio_protocol_t gpio_protos[GPIO_COUNT];
  bool has_debug_gpio = false;

  status = device_get_protocol(fragments[FRAGMENT_SDIO_FN1], ZX_PROTOCOL_SDIO, &sdio_proto_fn1);
  if (status != ZX_OK) {
    BRCMF_ERR("ZX_PROTOCOL_SDIO not found, err=%d", status);
    return status;
  }
  status = device_get_protocol(fragments[FRAGMENT_SDIO_FN2], ZX_PROTOCOL_SDIO, &sdio_proto_fn2);
  if (status != ZX_OK) {
    BRCMF_ERR("ZX_PROTOCOL_SDIO not found, err=%d", status);
    return status;
  }
  status = device_get_protocol(fragments[FRAGMENT_OOB_GPIO], ZX_PROTOCOL_GPIO,
                               &gpio_protos[WIFI_OOB_IRQ_GPIO_INDEX]);
  if (status != ZX_OK) {
    BRCMF_ERR("ZX_PROTOCOL_GPIO not found, err=%d", status);
    return status;
  }
  // Debug GPIO is optional
  if (fragment_count > 3) {
    status = device_get_protocol(fragments[FRAGMENT_DEBUG_GPIO], ZX_PROTOCOL_GPIO,
                                 &gpio_protos[DEBUG_GPIO_INDEX]);
    if (status != ZX_OK) {
      BRCMF_ERR("ZX_PROTOCOL_GPIO not found, err=%d", status);
      return status;
    }
    has_debug_gpio = true;
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

  err = zx::vmo::create(kDmaInitialBufferSize, ZX_VMO_RESIZABLE, &sdiodev->dma_buffer);
  if (err != ZX_OK) {
    BRCMF_ERR("Error creating DMA buffer: %s", zx_status_get_string(err));
    goto fail;
  }
  sdiodev->dma_buffer_size = kDmaInitialBufferSize;

  brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_DOWN);

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
