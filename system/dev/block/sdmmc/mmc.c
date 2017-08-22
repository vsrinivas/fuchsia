// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/protocol/sdmmc.h>

#include <pretty/hexdump.h>

#include "sdmmc.h"

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

static mx_status_t mmc_send_op_cond(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t ocr, uint32_t* rocr) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st;
    // Request sector addressing if not probing
    uint32_t arg = (ocr == 0) ? ocr : ((1 << 30) | ocr);
    for (int i = 100; i; i--) {
        if ((st = sdmmc_do_command(sdmmc->host_mxdev, MMC_SEND_OP_COND, arg, txn)) != MX_OK) {
            // fail on txn error
            break;
        }
        // No need to wait for busy clear if probing
        if ((arg == 0) || (pdata->response[0] & (1 << 31))) {
            *rocr = pdata->response[0];
            break;
        }
        mx_nanosleep(mx_deadline_after(MX_MSEC(10)));
    }
    return st;
}

static mx_status_t mmc_all_send_cid(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t cid[4]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st;
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, MMC_ALL_SEND_CID, 0, txn)) == MX_OK) {
        cid[0] = pdata->response[0];
        cid[1] = pdata->response[1];
        cid[2] = pdata->response[2];
        cid[3] = pdata->response[3];
    }
    return st;
}

static mx_status_t mmc_set_relative_addr(sdmmc_t* sdmmc, iotxn_t* txn, uint16_t rca) {
    return sdmmc_do_command(sdmmc->host_mxdev, MMC_SET_RELATIVE_ADDR, (rca << 16), txn);
}

static mx_status_t mmc_send_csd(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t csd[4]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st;
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, MMC_SEND_CSD, sdmmc->rca << 16, txn)) == MX_OK) {
        csd[0] = pdata->response[0];
        csd[1] = pdata->response[1];
        csd[2] = pdata->response[2];
        csd[3] = pdata->response[3];
    }
    return st;
}

static mx_status_t mmc_send_ext_csd(sdmmc_t* sdmmc, iotxn_t* txn, uint8_t ext_csd[512]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st;
    // EXT_CSD is send in a data stage
    pdata->blockcount = 1;
    pdata->blocksize = 512;
    txn->length = 512;
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, MMC_SEND_EXT_CSD, 0, txn)) == MX_OK) {
        iotxn_copyfrom(txn, ext_csd, 512, 0);
#if 0
        xprintf("EXT_CSD:\n");
        hexdump8_ex(ext_csd, 512, 0);
#endif
    }
    pdata->blockcount = 0;
    pdata->blocksize = 0;
    txn->length = 0;
    return st;
}

static mx_status_t mmc_select_card(sdmmc_t* sdmmc, iotxn_t* txn) {
    return sdmmc_do_command(sdmmc->host_mxdev, MMC_SELECT_CARD, sdmmc->rca << 16, txn);
}

static mx_status_t mmc_switch(sdmmc_t* sdmmc, iotxn_t* txn, uint8_t index, uint8_t value) {
    uint32_t arg = (3 << 24) |  // write byte
                   (index << 16) | (value << 8);
    return sdmmc_do_command(sdmmc->host_mxdev, MMC_SWITCH, arg, txn);
}

static mx_status_t mmc_send_status(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t* status) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st = sdmmc_do_command(sdmmc->host_mxdev, MMC_SEND_STATUS, sdmmc->rca << 16, txn);
    if (st == MX_OK) {
        *status = pdata->response[0];
    }
    return st;
}

static uint8_t mmc_select_bus_width(sdmmc_t* sdmmc, iotxn_t* txn) {
    mx_status_t st;
    // TODO verify host 8-bit support
    unsigned bus_widths[] = { SDMMC_BUS_WIDTH_8, MMC_EXT_CSD_BUS_WIDTH_8,
                              SDMMC_BUS_WIDTH_4, MMC_EXT_CSD_BUS_WIDTH_4 };
    for (unsigned i = 0; i < sizeof(bus_widths)/sizeof(unsigned); i += 2) {
        // Switch the card to the new bus width
        if ((st = mmc_switch(sdmmc, txn, MMC_EXT_CSD_BUS_WIDTH, bus_widths[i+1])) != MX_OK) {
            xprintf("sdmmc: failed to MMC_SWITCH bus width to EXT_CSD %d, retcode = %d\n", bus_widths[i+1], st);
            continue;
        }

        // Check status after MMC_SWITCH
        uint32_t status;
        if ((st = mmc_send_status(sdmmc, txn, &status)) != MX_OK) {
            xprintf("sdmmc: failed to MMC_SEND_STATUS (bus width %d), retcode = %d\n", bus_widths[i], st);
            continue;
        }
        if (status & MMC_STATUS_SWITCH_ERR) {
            xprintf("sdmmc: mmc status error after MMC_SWITCH (bus width %d), status = 0x%08x\n", bus_widths[i], status);
            continue;
        }

        // Switch the host to the new bus width
        uint32_t new_bus_width = bus_widths[i];
        if ((st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_BUS_WIDTH, &new_bus_width, sizeof(new_bus_width), NULL, 0, NULL)) != MX_OK) {
            xprintf("sdmmc: failed to switch the host bus width to %d, retcode = %d\n", bus_widths[i], st);
            continue;
        }

        // Read EXT_CSD again with the new bus width and compare
        uint8_t new_ext_csd[512];
        if ((st = mmc_send_ext_csd(sdmmc, txn, new_ext_csd)) != MX_OK) {
            xprintf("sdmmc: failed to get EXT_CSD after switching bus width to %d, retcode = %d\n", bus_widths[i], st);
            continue;
        }
        // Don't compare the BUS_WIDTH field because we just wrote to it
        // TODO just compare the read-only fields
        bool err = false;
        for (unsigned j = 0; j < sizeof(new_ext_csd); j++) {
            if (j == MMC_EXT_CSD_BUS_WIDTH) {
                continue;
            }
            if (new_ext_csd[j] != sdmmc->raw_ext_csd[j]) {
                err = true;
                break;
            }
        }
        if (err) {
            xprintf("sdmmc: failed to switch to bus width %d\n", bus_widths[i]);
            continue;
        }

        sdmmc->bus_width = bus_widths[i];
        break; // successfully set bus width so no need to loop
    }
    return sdmmc->bus_width;
}

static mx_status_t mmc_switch_timing(sdmmc_t* sdmmc, iotxn_t* txn, uint8_t new_timing) {
    if (new_timing > MMC_EXT_CSD_HS_TIMING_HS400) {
        return MX_ERR_INVALID_ARGS;
    }

    // Switch the device timing
    uint8_t ext_csd_timing[] = {
        MMC_EXT_CSD_HS_TIMING_LEGACY,
        MMC_EXT_CSD_HS_TIMING_HS,
        MMC_EXT_CSD_HS_TIMING_HS200,
        MMC_EXT_CSD_HS_TIMING_HS400
    };
    mx_status_t st = mmc_switch(sdmmc, txn, MMC_EXT_CSD_HS_TIMING, ext_csd_timing[new_timing]);
    if (st != MX_OK) {
        xprintf("sdmmc: failed to switch device timing to %d\n", new_timing);
        return st;
    }

    // Switch the host timing
    uint32_t arg = new_timing;
    if ((st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_TIMING, &arg, sizeof(arg), NULL, 0, NULL)) != MX_OK) {
        xprintf("sdmmc: failed to switch host timing to %d\n", new_timing);
        return st;
    }

    // Check status after MMC_SWITCH
    uint32_t status;
    if ((st = mmc_send_status(sdmmc, txn, &status)) != MX_OK) {
        return st;
    }
    if (status & MMC_STATUS_SWITCH_ERR) {
        xprintf("sdmmc: mmc status error after MMC_SWITCH, status = 0x%08x\n", status);
        return MX_ERR_INTERNAL;
    }

    sdmmc->timing = new_timing;
    return st;
}

static mx_status_t mmc_decode_cid(sdmmc_t* sdmmc, const uint8_t* raw_cid) {
    printf("sdmmc: product name=%c%c%c%c%c%c\n",
            raw_cid[6], raw_cid[7], raw_cid[8], raw_cid[9], raw_cid[10], raw_cid[11]);
    printf("       revision=%u.%u\n", (raw_cid[5] >> 4) & 0xf, raw_cid[5] & 0xf);
    printf("       serial=%u\n", *((uint32_t*)&raw_cid[1]));
    return MX_OK;
}

static mx_status_t mmc_decode_csd(sdmmc_t* sdmmc, const uint8_t* raw_csd) {
    uint8_t spec_vrsn = (raw_csd[14] >> 2) & 0xf;
    // Only support spec version > 4.0
    if (spec_vrsn < MMC_CID_SPEC_VRSN_40) {
        return MX_ERR_NOT_SUPPORTED;
    }

    xprintf("sdmmc: CSD version %u spec version %u\n", (raw_csd[14] >> 6) & 0x3, spec_vrsn);
#if 0
    xprintf("CSD:\n");
    hexdump8_ex(raw_csd, 16, 0);
#endif

    // Only support high capacity (> 2GB) cards
    uint16_t c_size = ((raw_csd[6] >> 6) & 0x3) |
                      (raw_csd[7] << 2) |
                      ((raw_csd[8] & 0x3) << 10);
    if (c_size != 0xfff) {
        xprintf("sdmmc: unsupported C_SIZE 0x%04x\n", c_size);
        return MX_ERR_NOT_SUPPORTED;
    }
    return MX_OK;
}

static mx_status_t mmc_decode_ext_csd(sdmmc_t* sdmmc, const uint8_t* raw_ext_csd) {
    xprintf("sdmmc: EXT_CSD version %u CSD version %u\n", raw_ext_csd[192], raw_ext_csd[194]);

    // Get the capacity for the card
    uint32_t sectors = (raw_ext_csd[212] << 0) | (raw_ext_csd[213] << 8) | (raw_ext_csd[214] << 16) | (raw_ext_csd[215] << 24);
    sdmmc->capacity = sectors * 512ul;

    printf("sdmmc: found card with capacity = %" PRIu64 "B\n", sdmmc->capacity);

    return MX_OK;
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

mx_status_t sdmmc_probe_mmc(sdmmc_t* sdmmc, iotxn_t* setup_txn) {
    mx_status_t st;

    // Query OCR
    uint32_t ocr = 0;
    if ((st = mmc_send_op_cond(sdmmc, setup_txn, ocr, &ocr)) != MX_OK) {
        xprintf("sdmmc: MMC_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    // Check if the card matches the host's supported voltages and indicate sector mode
    // TODO check with host
    if ((st = mmc_send_op_cond(sdmmc, setup_txn, ocr, &ocr)) != MX_OK) {
        xprintf("sdmmc: MMC_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    // Get CID from card
    // Only 1 card on eve so no need to loop
    if ((st = mmc_all_send_cid(sdmmc, setup_txn, sdmmc->raw_cid)) != MX_OK) {
        xprintf("sdmmc: MMC_ALL_SEND_CID failed, retcode = %d\n", st);
        goto err;
    }
    xprintf("sdmmc: MMC_ALL_SEND_CID cid 0x%08x 0x%08x 0x%08x 0x%08x\n",
        sdmmc->raw_cid[0],
        sdmmc->raw_cid[1],
        sdmmc->raw_cid[2],
        sdmmc->raw_cid[3]);

    mmc_decode_cid(sdmmc, (const uint8_t*)sdmmc->raw_cid);

    // Set relative card address
    sdmmc->rca = 1;
    if ((st = mmc_set_relative_addr(sdmmc, setup_txn, sdmmc->rca)) != MX_OK) {
        xprintf("sdmmc: MMC_SET_RELATIVE_ADDR failed, retcode = %d\n", st);
        goto err;
    }

    // Read CSD register
    if ((st = mmc_send_csd(sdmmc, setup_txn, sdmmc->raw_csd)) != MX_OK) {
        xprintf("sdmmc: MMC_SEND_CSD failed, retcode = %d\n", st);
        goto err;
    }

    if ((st = mmc_decode_csd(sdmmc, (const uint8_t*)sdmmc->raw_csd)) != MX_OK) {
        goto err;
    }

    // Select the card
    if ((st = mmc_select_card(sdmmc, setup_txn)) != MX_OK) {
        xprintf("sdmmc: MMC_SELECT_CARD failed, retcode = %d\n", st);
        goto err;
    }

    // Read extended CSD register
    if ((st = mmc_send_ext_csd(sdmmc, setup_txn, sdmmc->raw_ext_csd)) != MX_OK) {
        xprintf("sdmmc: MMC_SEND_EXT_CSD failed, retcode = %d\n", st);
        goto err;
    }

    if ((st = mmc_decode_ext_csd(sdmmc, (const uint8_t*)sdmmc->raw_ext_csd)) != MX_OK) {
        goto err;
    }

    sdmmc->type = SDMMC_TYPE_MMC;
    sdmmc->bus_width = SDMMC_BUS_WIDTH_1;
    sdmmc->signal_voltage = SDMMC_SIGNAL_VOLTAGE_330; // TODO verify with host

    // Switch to high-speed timing
    if (mmc_supports_hs(sdmmc)) {
        // Switch to 1.8V signal voltage
        const uint32_t new_voltage = SDMMC_SIGNAL_VOLTAGE_180;
        if ((st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_SIGNAL_VOLTAGE, &new_voltage,
                               sizeof(new_voltage), NULL, 0, NULL)) != MX_OK) {
            xprintf("sdmmc: failed to switch to 1.8V signalling, retcode = %d\n", st);
            goto err;
        }
        sdmmc->signal_voltage = new_voltage;

        // Switch to widest supported bus width
        uint32_t new_bus_width = mmc_select_bus_width(sdmmc, setup_txn);
        if (new_bus_width == SDMMC_BUS_WIDTH_1) {
            xprintf("sdmmc: failed to select bus width\n");
            goto err;
        }

        // If successfully switched to 4- or 8-bit bus, switch to high-speed timing
        if ((st = mmc_switch_timing(sdmmc, setup_txn, SDMMC_TIMING_HS)) != MX_OK) {
            xprintf("sdmmc: failed to switch to high-speed timing\n");
            goto err;
        }

        // Set the bus frequency to high-speed timing
        uint32_t hs_freq = 52000000; // 52 mhz
        if ((st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_BUS_FREQ, &hs_freq, sizeof(hs_freq), NULL, 0, NULL)) != MX_OK) {
            xprintf("sdmmc: failed to set host bus frequency, retcode = %d\n", st);
            goto err;
        }
        sdmmc->clock_rate = hs_freq;
    } else {
        // Set the bus frequency to legacy timing
        uint32_t bus_freq = 25000000; // 25 mhz
        if ((st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_BUS_FREQ, &bus_freq, sizeof(bus_freq), NULL, 0, NULL)) != MX_OK) {
            xprintf("sdmmc: failed to set host bus frequency, retcode = %d\n", st);
            goto err;
        }
        sdmmc->clock_rate = bus_freq;
        sdmmc->timing = SDMMC_TIMING_LEGACY;
    }

    xprintf("sdmmc: initialized mmc @ %u mhz bus width %d\n", sdmmc->clock_rate, sdmmc->bus_width);

err:
    return st;
}
