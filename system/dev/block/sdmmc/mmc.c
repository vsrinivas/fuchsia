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

#define TIMING_200MHZ 200000000
#define TIMING_52MHZ 52000000
#define TIMING_25MHZ 25000000

static zx_status_t mmc_send_op_cond(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t ocr, uint32_t* rocr) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    zx_status_t st;
    // Request sector addressing if not probing
    uint32_t arg = (ocr == 0) ? ocr : ((1 << 30) | ocr);
    for (int i = 100; i; i--) {
        if ((st = sdmmc_do_command(sdmmc->host_zxdev, MMC_SEND_OP_COND, arg, txn)) != ZX_OK) {
            // fail on txn error
            break;
        }
        // No need to wait for busy clear if probing
        if ((arg == 0) || (pdata->response[0] & (1 << 31))) {
            *rocr = pdata->response[0];
            break;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    }
    return st;
}

static zx_status_t mmc_all_send_cid(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t cid[4]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    zx_status_t st;
    if ((st = sdmmc_do_command(sdmmc->host_zxdev, MMC_ALL_SEND_CID, 0, txn)) == ZX_OK) {
        cid[0] = pdata->response[0];
        cid[1] = pdata->response[1];
        cid[2] = pdata->response[2];
        cid[3] = pdata->response[3];
    }
    return st;
}

static zx_status_t mmc_set_relative_addr(sdmmc_t* sdmmc, iotxn_t* txn, uint16_t rca) {
    return sdmmc_do_command(sdmmc->host_zxdev, MMC_SET_RELATIVE_ADDR, (rca << 16), txn);
}

static zx_status_t mmc_send_csd(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t csd[4]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    zx_status_t st;
    if ((st = sdmmc_do_command(sdmmc->host_zxdev, MMC_SEND_CSD, sdmmc->rca << 16, txn)) == ZX_OK) {
        csd[0] = pdata->response[0];
        csd[1] = pdata->response[1];
        csd[2] = pdata->response[2];
        csd[3] = pdata->response[3];
    }
    return st;
}

static zx_status_t mmc_send_ext_csd(sdmmc_t* sdmmc, iotxn_t* txn, uint8_t ext_csd[512]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    zx_status_t st;
    // EXT_CSD is send in a data stage
    pdata->blockcount = 1;
    pdata->blocksize = 512;
    txn->length = 512;
    if ((st = sdmmc_do_command(sdmmc->host_zxdev, MMC_SEND_EXT_CSD, 0, txn)) == ZX_OK) {
        iotxn_copyfrom(txn, ext_csd, 512, 0);

        if (driver_get_log_flags() & DDK_LOG_SPEW) {
            zxlogf(SPEW, "EXT_CSD:\n");
            hexdump8_ex(ext_csd, 512, 0);
        }
    }
    pdata->blockcount = 0;
    pdata->blocksize = 0;
    txn->length = 0;
    return st;
}

static zx_status_t mmc_select_card(sdmmc_t* sdmmc, iotxn_t* txn) {
    return sdmmc_do_command(sdmmc->host_zxdev, MMC_SELECT_CARD, sdmmc->rca << 16, txn);
}

static zx_status_t mmc_switch(sdmmc_t* sdmmc, iotxn_t* txn, uint8_t index, uint8_t value) {
    // Send the MMC_SWITCH command
    uint32_t arg = (3 << 24) |  // write byte
                   (index << 16) | (value << 8);
    zx_status_t st;
    if ((st = sdmmc_do_command(sdmmc->host_zxdev, MMC_SWITCH, arg, txn)) != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to send MMC_SWITCH, status = %d\n", st);
        return st;
    }

    // Check status after MMC_SWITCH
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    st = sdmmc_do_command(sdmmc->host_zxdev, MMC_SEND_STATUS, sdmmc->rca << 16, txn);
    if (st == ZX_OK) {
        if (pdata->response[0] & MMC_STATUS_SWITCH_ERR) {
            zxlogf(ERROR, "mmc: mmc status error after MMC_SWITCH, status = 0x%08x\n",
                    pdata->response[0]);
            st = ZX_ERR_INTERNAL;
        }
    } else {
        zxlogf(ERROR, "mmc: failed to MMC_SEND_STATUS (%x=%d), retcode = %d\n", index, value, st);
    }

    return ZX_OK;
}

static zx_status_t mmc_set_bus_width(
        sdmmc_t* sdmmc, iotxn_t* txn, uint8_t sdmmc_width, uint8_t mmc_width) {
    zx_status_t st;
    // Switch the card to the new bus width
    if ((st = mmc_switch(sdmmc, txn, MMC_EXT_CSD_BUS_WIDTH, mmc_width)) != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to MMC_SWITCH bus width to EXT_CSD %d, retcode = %d\n",
                mmc_width, st);
        return ZX_ERR_INTERNAL;
    }

    if (sdmmc_width != sdmmc->bus_width) {
        // Switch the host to the new bus width
        uint32_t new_bus_width = sdmmc_width;
        if ((st = device_ioctl(sdmmc->host_zxdev, IOCTL_SDMMC_SET_BUS_WIDTH,
                        &new_bus_width, sizeof(new_bus_width), NULL, 0, NULL)) != ZX_OK) {
            zxlogf(ERROR, "mmc: failed to switch the host bus width to %d, retcode = %d\n",
                    sdmmc_width, st);
            return ZX_ERR_INTERNAL;
        }
    }

    sdmmc->bus_width = sdmmc_width;
    return ZX_OK;
}

static uint8_t mmc_select_bus_width(sdmmc_t* sdmmc, iotxn_t* txn) {
    // TODO verify host 8-bit support
    uint8_t bus_widths[] = { SDMMC_BUS_WIDTH_8, MMC_EXT_CSD_BUS_WIDTH_8,
                              SDMMC_BUS_WIDTH_4, MMC_EXT_CSD_BUS_WIDTH_4,
                              SDMMC_BUS_WIDTH_1, MMC_EXT_CSD_BUS_WIDTH_1 };
    for (unsigned i = 0; i < sizeof(bus_widths)/sizeof(unsigned); i += 2) {
        if (mmc_set_bus_width(sdmmc, txn, bus_widths[i], bus_widths[i+1]) == ZX_OK) {
            break;
        }
    }
    return sdmmc->bus_width;
}

static zx_status_t mmc_switch_timing(sdmmc_t* sdmmc, iotxn_t* txn, uint8_t new_timing) {
    // Switch the device timing
    uint8_t ext_csd_timing[] = {
        MMC_EXT_CSD_HS_TIMING_LEGACY,
        MMC_EXT_CSD_HS_TIMING_HS,
        MMC_EXT_CSD_HS_TIMING_HS,  // sdhci has a different timing constant for HSDDR vs HS
        MMC_EXT_CSD_HS_TIMING_HS200,
        MMC_EXT_CSD_HS_TIMING_HS400
    };
    if (new_timing > sizeof(ext_csd_timing)/sizeof(ext_csd_timing[0])) {
        zxlogf(ERROR, "mmc: invalid arg %d\n", new_timing);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t st = mmc_switch(sdmmc, txn, MMC_EXT_CSD_HS_TIMING, ext_csd_timing[new_timing]);
    if (st != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to switch device timing to %d\n", new_timing);
        return st;
    }

    // Switch the host timing
    uint32_t arg = new_timing;
    if ((st = device_ioctl(sdmmc->host_zxdev, IOCTL_SDMMC_SET_TIMING,
                    &arg, sizeof(arg), NULL, 0, NULL)) != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to switch host timing to %d\n", new_timing);
        return st;
    }

    sdmmc->timing = new_timing;
    return st;
}

static zx_status_t mmc_switch_freq(sdmmc_t* sdmmc, uint32_t new_freq) {
    zx_status_t st;
    if ((st = device_ioctl(sdmmc->host_zxdev, IOCTL_SDMMC_SET_BUS_FREQ,
                    &new_freq, sizeof(new_freq), NULL, 0, NULL)) != ZX_OK) {
        zxlogf(ERROR, "mmc: failed to set host bus frequency, retcode = %d\n", st);
        return st;
    }
    sdmmc->clock_rate = new_freq;
    return ZX_OK;
}

static zx_status_t mmc_decode_cid(sdmmc_t* sdmmc, const uint8_t* raw_cid) {
    printf("mmc: product name=%c%c%c%c%c%c\n",
            raw_cid[6], raw_cid[7], raw_cid[8], raw_cid[9], raw_cid[10], raw_cid[11]);
    printf("       revision=%u.%u\n", (raw_cid[5] >> 4) & 0xf, raw_cid[5] & 0xf);
    printf("       serial=%u\n", *((uint32_t*)&raw_cid[1]));
    return ZX_OK;
}

static zx_status_t mmc_decode_csd(sdmmc_t* sdmmc, const uint8_t* raw_csd) {
    uint8_t spec_vrsn = (raw_csd[14] >> 2) & 0xf;
    // Only support spec version > 4.0
    if (spec_vrsn < MMC_CID_SPEC_VRSN_40) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zxlogf(SPEW, "mmc: CSD version %u spec version %u\n", (raw_csd[14] >> 6) & 0x3, spec_vrsn);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        zxlogf(SPEW, "CSD:\n");
        hexdump8_ex(raw_csd, 16, 0);
    }

    // Only support high capacity (> 2GB) cards
    uint16_t c_size = ((raw_csd[6] >> 6) & 0x3) |
                      (raw_csd[7] << 2) |
                      ((raw_csd[8] & 0x3) << 10);
    if (c_size != 0xfff) {
        zxlogf(ERROR, "mmc: unsupported C_SIZE 0x%04x\n", c_size);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

static zx_status_t mmc_decode_ext_csd(sdmmc_t* sdmmc, const uint8_t* raw_ext_csd) {
    zxlogf(SPEW, "mmc: EXT_CSD version %u CSD version %u\n", raw_ext_csd[192], raw_ext_csd[194]);

    // Get the capacity for the card
    uint32_t sectors = (raw_ext_csd[212] << 0) | (raw_ext_csd[213] << 8) | (raw_ext_csd[214] << 16) | (raw_ext_csd[215] << 24);
    sdmmc->capacity = sectors * 512ul;

    zxlogf(TRACE, "mmc: found card with capacity = %" PRIu64 "B\n", sdmmc->capacity);

    return ZX_OK;
}

static bool mmc_supports_hs(sdmmc_t* sdmmc) {
    uint8_t device_type = sdmmc->raw_ext_csd[MMC_EXT_CSD_DEVICE_TYPE];
    return (device_type & (1 << 1));
}

static bool mmc_supports_hsddr(sdmmc_t* sdmmc) {
    uint8_t device_type = sdmmc->raw_ext_csd[MMC_EXT_CSD_DEVICE_TYPE];
    // Only support HSDDR @ 1.8V/3V
    return (device_type & (1 << 2));
}

static bool mmc_supports_hs200(sdmmc_t* sdmmc) {
    uint8_t device_type = sdmmc->raw_ext_csd[MMC_EXT_CSD_DEVICE_TYPE];
    // Only support HS200 @ 1.8V
    return (device_type & (1 << 4));
}

static bool mmc_supports_hs400(sdmmc_t* sdmmc) {
    uint8_t device_type = sdmmc->raw_ext_csd[MMC_EXT_CSD_DEVICE_TYPE];
    // Only support HS400 @ 1.8V
    return (device_type & (1 << 6));
}

zx_status_t sdmmc_probe_mmc(sdmmc_t* sdmmc, iotxn_t* setup_txn) {
    zx_status_t st;

    // Query OCR
    uint32_t ocr = 0;
    if ((st = mmc_send_op_cond(sdmmc, setup_txn, ocr, &ocr)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    // Check if the card matches the host's supported voltages and indicate sector mode
    // TODO check with host
    if ((st = mmc_send_op_cond(sdmmc, setup_txn, ocr, &ocr)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    // Get CID from card
    // Only 1 card on eve so no need to loop
    if ((st = mmc_all_send_cid(sdmmc, setup_txn, sdmmc->raw_cid)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_ALL_SEND_CID failed, retcode = %d\n", st);
        goto err;
    }
    zxlogf(SPEW, "mmc: MMC_ALL_SEND_CID cid 0x%08x 0x%08x 0x%08x 0x%08x\n",
        sdmmc->raw_cid[0],
        sdmmc->raw_cid[1],
        sdmmc->raw_cid[2],
        sdmmc->raw_cid[3]);

    mmc_decode_cid(sdmmc, (const uint8_t*)sdmmc->raw_cid);

    // Set relative card address
    sdmmc->rca = 1;
    if ((st = mmc_set_relative_addr(sdmmc, setup_txn, sdmmc->rca)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SET_RELATIVE_ADDR failed, retcode = %d\n", st);
        goto err;
    }

    // Read CSD register
    if ((st = mmc_send_csd(sdmmc, setup_txn, sdmmc->raw_csd)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SEND_CSD failed, retcode = %d\n", st);
        goto err;
    }

    if ((st = mmc_decode_csd(sdmmc, (const uint8_t*)sdmmc->raw_csd)) != ZX_OK) {
        goto err;
    }

    // Select the card
    if ((st = mmc_select_card(sdmmc, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SELECT_CARD failed, retcode = %d\n", st);
        goto err;
    }

    // Read extended CSD register
    if ((st = mmc_send_ext_csd(sdmmc, setup_txn, sdmmc->raw_ext_csd)) != ZX_OK) {
        zxlogf(ERROR, "mmc: MMC_SEND_EXT_CSD failed, retcode = %d\n", st);
        goto err;
    }

    if ((st = mmc_decode_ext_csd(sdmmc, (const uint8_t*)sdmmc->raw_ext_csd)) != ZX_OK) {
        goto err;
    }

    sdmmc->type = SDMMC_TYPE_MMC;
    sdmmc->bus_width = SDMMC_BUS_WIDTH_1;
    sdmmc->signal_voltage = SDMMC_SIGNAL_VOLTAGE_330; // TODO verify with host

    // Switch to high-speed timing
    if (mmc_supports_hs(sdmmc) || mmc_supports_hsddr(sdmmc) || mmc_supports_hs200(sdmmc)) {
        // Switch to 1.8V signal voltage
        const uint32_t new_voltage = SDMMC_SIGNAL_VOLTAGE_180;
        if ((st = device_ioctl(sdmmc->host_zxdev, IOCTL_SDMMC_SET_SIGNAL_VOLTAGE, &new_voltage,
                               sizeof(new_voltage), NULL, 0, NULL)) != ZX_OK) {
            zxlogf(ERROR, "mmc: failed to switch to 1.8V signalling, retcode = %d\n", st);
            goto err;
        }
        sdmmc->signal_voltage = new_voltage;

        mmc_select_bus_width(sdmmc, setup_txn);

        if (mmc_supports_hs200(sdmmc) && sdmmc->bus_width != SDMMC_BUS_WIDTH_1) {
            if ((st = mmc_switch_timing(sdmmc, setup_txn, SDMMC_TIMING_HS200)) != ZX_OK) {
                goto err;
            }

            if ((st = mmc_switch_freq(sdmmc, TIMING_200MHZ)) != ZX_OK) {
                goto err;
            }

            if ((st = device_ioctl(sdmmc->host_zxdev, IOCTL_SDMMC_MMC_TUNING,
                            NULL, 0, NULL, 0, NULL)) != ZX_OK) {
                zxlogf(ERROR, "mmc: tuning failed %d\n", st);
                goto err;
            }

            if (mmc_supports_hs400(sdmmc) && sdmmc->bus_width == SDMMC_BUS_WIDTH_8) {
                if ((st = mmc_switch_timing(sdmmc, setup_txn, SDMMC_TIMING_HS)) != ZX_OK) {
                    goto err;
                }

                if ((st = mmc_switch_freq(sdmmc, TIMING_52MHZ)) != ZX_OK) {
                    goto err;
                }

                if (mmc_set_bus_width(sdmmc, setup_txn,
                            SDMMC_BUS_WIDTH_8, MMC_EXT_CSD_BUS_WIDTH_8_DDR) != ZX_OK) {
                    goto err;
                }

                if ((st = mmc_switch_timing(sdmmc, setup_txn, SDMMC_TIMING_HS400)) != ZX_OK) {
                    goto err;
                }

                if ((st = mmc_switch_freq(sdmmc, TIMING_200MHZ)) != ZX_OK) {
                    goto err;
                }
            }
        } else {
            if ((st = mmc_switch_timing(sdmmc, setup_txn, SDMMC_TIMING_HS)) != ZX_OK) {
                goto err;
            }

            if (mmc_supports_hsddr(sdmmc) && sdmmc->bus_width != SDMMC_BUS_WIDTH_1) {
                if ((st = mmc_switch_timing(sdmmc, setup_txn, SDMMC_TIMING_HSDDR)) != ZX_OK) {
                    goto err;
                }

                uint8_t mmc_bus_width = (sdmmc->bus_width == SDMMC_BUS_WIDTH_4) ?
                        MMC_EXT_CSD_BUS_WIDTH_4_DDR : MMC_EXT_CSD_BUS_WIDTH_8_DDR;
                if ((st = mmc_set_bus_width(
                                sdmmc, setup_txn, sdmmc->bus_width, mmc_bus_width)) != ZX_OK) {
                    goto err;
                }
            }

            if ((st = mmc_switch_freq(sdmmc, TIMING_52MHZ)) != ZX_OK) {
                goto err;
            }
        }
    } else {
        // Set the bus frequency to legacy timing
        if ((st = mmc_switch_freq(sdmmc, TIMING_25MHZ)) != ZX_OK) {
            goto err;
        }
        sdmmc->timing = SDMMC_TIMING_LEGACY;
    }

    zxlogf(INFO, "mmc: initialized mmc @ %u mhz, bus width %d, timing %d\n",
            sdmmc->clock_rate, sdmmc->bus_width, sdmmc->timing);

err:
    return st;
}
