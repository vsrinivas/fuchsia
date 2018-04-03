// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/debug.h>
#include <ddk/protocol/sdmmc.h>

#include <pretty/hexdump.h>

#include "sdmmc.h"

#define FREQ_200MHZ 200000000
#define FREQ_52MHZ 52000000
#define FREQ_25MHZ 25000000

#define MMC_SECTOR_SIZE  512ul  // physical sector size
#define MMC_BLOCK_SIZE   512ul  // block size is 512 bytes always because it is the required
                                // value if the card is in DDR mode

static zx_status_t mmc_do_switch(sdmmc_device_t* dev, uint8_t index, uint8_t value) {
    // Send the MMC_SWITCH command
    zx_status_t st = mmc_switch(dev, index, value);
    if (st != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to MMC_SWITCH (0x%x=%d), retcode = %d\n", index, value, st);
        return st;
    }

    // Check status after MMC_SWITCH
    uint32_t resp;
    st = sdmmc_send_status(dev, &resp);
    if (st == ZX_OK) {
        if (resp & MMC_STATUS_SWITCH_ERR) {
            zxlogf(ERROR, "mmc: mmc status error after MMC_SWITCH (0x%x=%d), status = 0x%08x\n",
                   index, value, resp);
            st = ZX_ERR_INTERNAL;
        }
    } else {
        zxlogf(ERROR, "mmc: failed to MMC_SEND_STATUS (%x=%d), retcode = %d\n", index, value, st);
    }

    return ZX_OK;
}

static zx_status_t mmc_set_bus_width(sdmmc_device_t* dev, sdmmc_bus_width_t bus_width,
                                     uint8_t mmc_ext_csd_bus_width) {
    // Switch the card to the new bus width
    zx_status_t st = mmc_do_switch(dev, MMC_EXT_CSD_BUS_WIDTH, mmc_ext_csd_bus_width);
    if (st != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to switch bus width to EXT_CSD %d, retcode = %d\n",
               mmc_ext_csd_bus_width, st);
        return ZX_ERR_INTERNAL;
    }

    if (bus_width != dev->bus_width) {
        // Switch the host to the new bus width
        if ((st = sdmmc_set_bus_width(&dev->host, bus_width)) != ZX_OK) {
            zxlogf(ERROR, "mmc: failed to switch the host bus width to %d, retcode = %d\n",
                   bus_width, st);
            return ZX_ERR_INTERNAL;
        }
    }

    dev->bus_width = bus_width;
    return ZX_OK;
}

static uint8_t mmc_select_bus_width(sdmmc_device_t* dev) {
    // TODO verify host 8-bit support
    uint8_t bus_widths[] = { SDMMC_BUS_WIDTH_8, MMC_EXT_CSD_BUS_WIDTH_8,
                             SDMMC_BUS_WIDTH_4, MMC_EXT_CSD_BUS_WIDTH_4,
                             SDMMC_BUS_WIDTH_1, MMC_EXT_CSD_BUS_WIDTH_1 };
    for (unsigned i = 0; i < sizeof(bus_widths)/sizeof(unsigned); i += 2) {
        if (mmc_set_bus_width(dev, bus_widths[i], bus_widths[i+1]) == ZX_OK) {
            break;
        }
    }
    return dev->bus_width;
}

static zx_status_t mmc_switch_timing(sdmmc_device_t* dev, sdmmc_timing_t new_timing) {
    // Switch the device timing
    uint8_t ext_csd_timing;
    switch (new_timing) {
    case SDMMC_TIMING_LEGACY:
        ext_csd_timing = MMC_EXT_CSD_HS_TIMING_LEGACY;
        break;
    case SDMMC_TIMING_HS:
        ext_csd_timing = MMC_EXT_CSD_HS_TIMING_HS;
        break;
    case SDMMC_TIMING_HSDDR:
        // sdhci has a different timing constant for HSDDR vs HS
        ext_csd_timing = MMC_EXT_CSD_HS_TIMING_HS;
        break;
    case SDMMC_TIMING_HS200:
        ext_csd_timing = MMC_EXT_CSD_HS_TIMING_HS200;
        break;
    case SDMMC_TIMING_HS400:
        ext_csd_timing = MMC_EXT_CSD_HS_TIMING_HS400;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    };

    zx_status_t st = mmc_do_switch(dev, MMC_EXT_CSD_HS_TIMING, ext_csd_timing);
    if (st != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to switch device timing to %d\n", new_timing);
        return st;
    }

    // Switch the host timing
    if ((st = sdmmc_set_timing(&dev->host, new_timing)) != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to switch host timing to %d\n", new_timing);
        return st;
    }

    dev->timing = new_timing;
    return st;
}

static zx_status_t mmc_switch_freq(sdmmc_device_t* dev, uint32_t new_freq) {
    zx_status_t st;
    if ((st = sdmmc_set_bus_freq(&dev->host, new_freq)) != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to set host bus frequency, retcode = %d\n", st);
        return st;
    }
    dev->clock_rate = new_freq;
    return ZX_OK;
}

static zx_status_t mmc_decode_cid(sdmmc_device_t* dev, const uint8_t* raw_cid) {
    printf("mmc: product name=%c%c%c%c%c%c\n",
            raw_cid[MMC_CID_PRODUCT_NAME_START], raw_cid[MMC_CID_PRODUCT_NAME_START + 1],
            raw_cid[MMC_CID_PRODUCT_NAME_START + 2], raw_cid[MMC_CID_PRODUCT_NAME_START + 3],
            raw_cid[MMC_CID_PRODUCT_NAME_START + 4], raw_cid[MMC_CID_PRODUCT_NAME_START + 5]);
    printf("       revision=%u.%u\n", (raw_cid[MMC_CID_REVISION] >> 4) & 0xf,
            raw_cid[MMC_CID_REVISION] & 0xf);
    printf("       serial=%u\n", *((uint32_t*)&raw_cid[MMC_CID_SERIAL]));
    return ZX_OK;
}

static zx_status_t mmc_decode_csd(sdmmc_device_t* dev, const uint8_t* raw_csd) {
    uint8_t spec_vrsn = (raw_csd[MMC_CSD_SPEC_VERSION] >> 2) & 0xf;
    // Only support spec version > 4.0
    if (spec_vrsn < MMC_CID_SPEC_VRSN_40) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zxlogf(SPEW, "mmc: CSD version %u spec version %u\n",
           (raw_csd[MMC_CSD_SPEC_VERSION] >> 6) & 0x3, spec_vrsn);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        zxlogf(SPEW, "CSD:\n");
        hexdump8_ex(raw_csd, 16, 0);
    }

    // Only support high capacity (> 2GB) cards
    uint16_t c_size = ((raw_csd[MMC_CSD_SIZE_START] >> 6) & 0x3) |
                      (raw_csd[MMC_CSD_SIZE_START + 1] << 2) |
                      ((raw_csd[MMC_CSD_SIZE_START + 2] & 0x3) << 10);
    if (c_size != 0xfff) {
        zxlogf(ERROR, "mmc: unsupported C_SIZE 0x%04x\n", c_size);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

static zx_status_t mmc_decode_ext_csd(sdmmc_device_t* dev, const uint8_t* raw_ext_csd) {
    zxlogf(SPEW, "mmc: EXT_CSD version %u CSD version %u\n", raw_ext_csd[192], raw_ext_csd[194]);

    // Get the capacity for the card
    uint32_t sectors = (raw_ext_csd[212] << 0) | (raw_ext_csd[213] << 8) |
                       (raw_ext_csd[214] << 16) | (raw_ext_csd[215] << 24);
    dev->block_info.block_count = sectors * MMC_SECTOR_SIZE / MMC_BLOCK_SIZE;
    dev->block_info.block_size = (uint32_t)MMC_BLOCK_SIZE;

    zxlogf(TRACE, "mmc: found card with capacity = %" PRIu64 "B\n",
           dev->block_info.block_count * dev->block_info.block_size);

    return ZX_OK;
}

static bool mmc_supports_hs(sdmmc_device_t* dev) {
    uint8_t device_type = dev->raw_ext_csd[MMC_EXT_CSD_DEVICE_TYPE];
    return (device_type & (1 << 1));
}

static bool mmc_supports_hsddr(sdmmc_device_t* dev) {
    uint8_t device_type = dev->raw_ext_csd[MMC_EXT_CSD_DEVICE_TYPE];
    // Only support HSDDR @ 1.8V/3V
    return (device_type & (1 << 2));
}

static bool mmc_supports_hs200(sdmmc_device_t* dev) {
    uint8_t device_type = dev->raw_ext_csd[MMC_EXT_CSD_DEVICE_TYPE];
    // Only support HS200 @ 1.8V
    return (device_type & (1 << 4));
}

static bool mmc_supports_hs400(sdmmc_device_t* dev) {
    uint8_t device_type = dev->raw_ext_csd[MMC_EXT_CSD_DEVICE_TYPE];
    // Only support HS400 @ 1.8V
    return (device_type & (1 << 6));
}

zx_status_t sdmmc_probe_mmc(sdmmc_device_t* dev) {
    zx_status_t st = ZX_OK;

    // Query OCR
    uint32_t ocr = 0;
    if ((st = mmc_send_op_cond(dev, ocr, &ocr)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    // Indicate sector mode
    if ((st = mmc_send_op_cond(dev, ocr, &ocr)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    // Get CID from card
    // Only supports 1 card currently so no need to loop
    if ((st = mmc_all_send_cid(dev, dev->raw_cid)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_ALL_SEND_CID failed, retcode = %d\n", st);
        goto err;
    }
    zxlogf(SPEW, "mmc: MMC_ALL_SEND_CID cid 0x%08x 0x%08x 0x%08x 0x%08x\n",
        dev->raw_cid[0],
        dev->raw_cid[1],
        dev->raw_cid[2],
        dev->raw_cid[3]);

    mmc_decode_cid(dev, (const uint8_t*)dev->raw_cid);

    // Set relative card address
    if ((st = mmc_set_relative_addr(dev, 1)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SET_RELATIVE_ADDR failed, retcode = %d\n", st);
        goto err;
    }
    dev->rca = 1;

    // Read CSD register
    if ((st = mmc_send_csd(dev, dev->raw_csd)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SEND_CSD failed, retcode = %d\n", st);
        goto err;
    }

    if ((st = mmc_decode_csd(dev, (const uint8_t*)dev->raw_csd)) != ZX_OK) {
        goto err;
    }

    // Select the card
    if ((st = mmc_select_card(dev)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SELECT_CARD failed, retcode = %d\n", st);
        goto err;
    }

    // Read extended CSD register
    if ((st = mmc_send_ext_csd(dev, dev->raw_ext_csd)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SEND_EXT_CSD failed, retcode = %d\n", st);
        goto err;
    }

    if ((st = mmc_decode_ext_csd(dev, (const uint8_t*)dev->raw_ext_csd)) != ZX_OK) {
        goto err;
    }

    dev->type = SDMMC_TYPE_MMC;
    dev->bus_width = SDMMC_BUS_WIDTH_1;
    dev->signal_voltage = SDMMC_VOLTAGE_330;

    // Switch to high-speed timing
    if (mmc_supports_hs(dev) || mmc_supports_hsddr(dev) || mmc_supports_hs200(dev)) {
        // Switch to 1.8V signal voltage
        sdmmc_voltage_t new_voltage = SDMMC_VOLTAGE_180;
        if ((st = sdmmc_set_signal_voltage(&dev->host, new_voltage)) != ZX_OK) {
            zxlogf(ERROR, "mmc: failed to switch to 1.8V signalling, retcode = %d\n", st);
            goto err;
        }
        dev->signal_voltage = new_voltage;

        mmc_select_bus_width(dev);

        // Must perform tuning at HS200 first if HS400 is supported
        if (mmc_supports_hs200(dev) && dev->bus_width != SDMMC_BUS_WIDTH_1) {
            if ((st = mmc_switch_timing(dev, SDMMC_TIMING_HS200)) != ZX_OK) {
                goto err;
            }

            if ((st = mmc_switch_freq(dev, FREQ_200MHZ)) != ZX_OK) {
                goto err;
            }

            if ((st = sdmmc_perform_tuning(&dev->host)) != ZX_OK) {
                zxlogf(ERROR, "mmc: tuning failed %d\n", st);
                goto err;
            }

            if (mmc_supports_hs400(dev) && dev->bus_width == SDMMC_BUS_WIDTH_8) {
                if ((st = mmc_switch_timing(dev, SDMMC_TIMING_HS)) != ZX_OK) {
                    goto err;
                }

                if ((st = mmc_switch_freq(dev, FREQ_52MHZ)) != ZX_OK) {
                    goto err;
                }

                if ((st = mmc_set_bus_width(dev, SDMMC_BUS_WIDTH_8,
                                            MMC_EXT_CSD_BUS_WIDTH_8_DDR)) != ZX_OK) {
                    goto err;
                }

                if ((st = mmc_switch_timing(dev, SDMMC_TIMING_HS400)) != ZX_OK) {
                    goto err;
                }

                if ((st = mmc_switch_freq(dev, FREQ_200MHZ)) != ZX_OK) {
                    goto err;
                }
            }
        } else {
            if ((st = mmc_switch_timing(dev, SDMMC_TIMING_HS)) != ZX_OK) {
                goto err;
            }

            if (mmc_supports_hsddr(dev) && (dev->bus_width != SDMMC_BUS_WIDTH_1)) {
                if ((st = mmc_switch_timing(dev, SDMMC_TIMING_HSDDR)) != ZX_OK) {
                    goto err;
                }

                uint8_t mmc_bus_width = (dev->bus_width == SDMMC_BUS_WIDTH_4) ?
                                            MMC_EXT_CSD_BUS_WIDTH_4_DDR :
                                            MMC_EXT_CSD_BUS_WIDTH_8_DDR;
                if ((st = mmc_set_bus_width(dev, dev->bus_width, mmc_bus_width)) != ZX_OK) {
                    goto err;
                }
            }

            if ((st = mmc_switch_freq(dev, FREQ_52MHZ)) != ZX_OK) {
                goto err;
            }
        }
    } else {
        // Set the bus frequency to legacy timing
        if ((st = mmc_switch_freq(dev, FREQ_25MHZ)) != ZX_OK) {
            goto err;
        }
        dev->timing = SDMMC_TIMING_LEGACY;
    }

    zxlogf(INFO, "mmc: initialized mmc @ %u mhz, bus width %d, timing %d\n",
            dev->clock_rate, dev->bus_width, dev->timing);

err:
    return st;
}
