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

#include "common.h"

#include <stdarg.h>
#include <string.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "core.h"
#include "debug.h"
#include "device.h"
#include "firmware.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "of.h"
#include "tracepoint.h"

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11 wireless LAN fullmac driver.");
MODULE_LICENSE("Dual BSD/GPL");

const uint8_t ALLFFMAC[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

#define BRCMF_DEFAULT_SCAN_CHANNEL_TIME 40
#define BRCMF_DEFAULT_SCAN_UNASSOC_TIME 40

/* default boost value for RSSI_DELTA in preferred join selection */
#define BRCMF_JOIN_PREF_RSSI_BOOST 8

#define BRCMF_DEFAULT_TXGLOM_SIZE 32 /* max tx frames in glom chain */

static int brcmf_sdiod_txglomsz = BRCMF_DEFAULT_TXGLOM_SIZE;
module_param_named(txglomsz, brcmf_sdiod_txglomsz, int, 0);
MODULE_PARM_DESC(txglomsz, "Maximum tx packet chain size [SDIO]");

/* Debug level configuration. See debug.h for bits, sysfs modifiable */
int brcmf_msg_filter;
module_param_named(debug, brcmf_msg_filter, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(debug, "Level of debug output");

static int brcmf_p2p_enable;
module_param_named(p2pon, brcmf_p2p_enable, int, 0);
MODULE_PARM_DESC(p2pon, "Enable legacy p2p management functionality");

static int brcmf_feature_disable;
module_param_named(feature_disable, brcmf_feature_disable, int, 0);
MODULE_PARM_DESC(feature_disable, "Disable features");

static char brcmf_firmware_path[BRCMF_FW_ALTPATH_LEN] = "brcmfmac/";
module_param_string(alternative_fw_path, brcmf_firmware_path, BRCMF_FW_ALTPATH_LEN, S_IRUSR);
MODULE_PARM_DESC(alternative_fw_path, "Alternative firmware path");

static int brcmf_fcmode;
module_param_named(fcmode, brcmf_fcmode, int, 0);
MODULE_PARM_DESC(fcmode, "Mode of firmware signalled flow control");

static int brcmf_roamoff;
module_param_named(roamoff, brcmf_roamoff, int, S_IRUSR);
MODULE_PARM_DESC(roamoff, "Do not use internal roaming engine");

#ifdef DEBUG
/* always succeed brcmf_bus_started() */
static int brcmf_ignore_probe_fail;
module_param_named(ignore_probe_fail, brcmf_ignore_probe_fail, int, 0);
MODULE_PARM_DESC(ignore_probe_fail, "always succeed probe for debugging");
#endif

struct brcmf_mp_global_t brcmf_mp_global;

void brcmf_c_set_joinpref_default(struct brcmf_if* ifp) {
    struct brcmf_join_pref_params join_pref_params[2];
    zx_status_t err;

    /* Setup join_pref to select target by RSSI (boost on 5GHz) */
    join_pref_params[0].type = BRCMF_JOIN_PREF_RSSI_DELTA;
    join_pref_params[0].len = 2;
    join_pref_params[0].rssi_gain = BRCMF_JOIN_PREF_RSSI_BOOST;
    join_pref_params[0].band = WLC_BAND_5G;

    join_pref_params[1].type = BRCMF_JOIN_PREF_RSSI;
    join_pref_params[1].len = 2;
    join_pref_params[1].rssi_gain = 0;
    join_pref_params[1].band = 0;
    err = brcmf_fil_iovar_data_set(ifp, "join_pref", join_pref_params, sizeof(join_pref_params));
    if (err != ZX_OK) {
        brcmf_err("Set join_pref error (%d)\n", err);
    }
}

static zx_status_t brcmf_c_download(struct brcmf_if* ifp, uint16_t flag,
                                    struct brcmf_dload_data_le* dload_buf, uint32_t len) {
    zx_status_t err;

    flag |= (DLOAD_HANDLER_VER << DLOAD_FLAG_VER_SHIFT);
    dload_buf->flag = flag;
    dload_buf->dload_type = DL_TYPE_CLM;
    dload_buf->len = len;
    dload_buf->crc = 0;
    len = sizeof(*dload_buf) + len - 1;

    err = brcmf_fil_iovar_data_set(ifp, "clmload", dload_buf, len);

    return err;
}

static zx_status_t brcmf_c_get_clm_name(struct brcmf_if* ifp, uint8_t* clm_name) {
    struct brcmf_bus* bus = ifp->drvr->bus_if;
    struct brcmf_rev_info* ri = &ifp->drvr->revinfo;
    uint8_t fw_name[BRCMF_FW_NAME_LEN];
    uint8_t* ptr;
    size_t len;
    zx_status_t err;

    memset(fw_name, 0, BRCMF_FW_NAME_LEN);
    err = brcmf_bus_get_fwname(bus, ri->chipnum, ri->chiprev, fw_name);
    if (err != ZX_OK) {
        brcmf_err("get firmware name failed (%d)\n", err);
        goto done;
    }

    /* generate CLM blob file name */
    ptr = (uint8_t*)strrchr((char*)fw_name, '.');
    if (!ptr) {
        err = ZX_ERR_NOT_FOUND;
        goto done;
    }

    len = ptr - fw_name + 1;
    if (len + strlen(".clm_blob") > BRCMF_FW_NAME_LEN) {
        err = ZX_ERR_BUFFER_TOO_SMALL;
    } else {
        strlcpy((char*)clm_name, (const char*)fw_name, len);
        strlcat((char*)clm_name, ".clm_blob", BRCMF_FW_NAME_LEN);
    }
done:
    return err;
}

static zx_status_t brcmf_c_process_clm_blob(struct brcmf_if* ifp) {
    //struct brcmf_device* dev = ifp->drvr->bus_if->dev;
    struct brcmf_dload_data_le* chunk_buf;
    const struct brcmf_firmware* clm = NULL;
    uint8_t clm_name[BRCMF_FW_NAME_LEN];
    uint32_t chunk_len;
    uint32_t datalen;
    uint32_t cumulative_len;
    uint16_t dl_flag = DL_BEGIN;
    uint32_t status;
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter\n");

    memset(clm_name, 0, BRCMF_FW_NAME_LEN);
    err = brcmf_c_get_clm_name(ifp, clm_name);
    if (err != ZX_OK) {
        brcmf_err("get CLM blob file name failed (%d)\n", err);
        return err;
    }

    brcmf_dbg(TEMP, "* * Would have requested firmware name %s", clm_name);
    err = ZX_ERR_INTERNAL;
//    err = request_firmware(&clm, clm_name, dev);
    if (err != ZX_OK) {
        brcmf_info("no clm_blob available(err=%d), device may have limited channels available\n",
                   err);
        return ZX_OK;
    }

    chunk_buf = calloc(1, sizeof(*chunk_buf) + MAX_CHUNK_LEN - 1);
    if (!chunk_buf) {
        err = ZX_ERR_NO_MEMORY;
        goto done;
    }

    datalen = clm->size;
    cumulative_len = 0;
    do {
        if (datalen > MAX_CHUNK_LEN) {
            chunk_len = MAX_CHUNK_LEN;
        } else {
            chunk_len = datalen;
            dl_flag |= DL_END;
        }
        memcpy(chunk_buf->data, clm->data + cumulative_len, chunk_len);

        err = brcmf_c_download(ifp, dl_flag, chunk_buf, chunk_len);

        dl_flag &= ~DL_BEGIN;

        cumulative_len += chunk_len;
        datalen -= chunk_len;
    } while ((datalen > 0) && (err == ZX_OK));

    if (err != ZX_OK) {
        brcmf_err("clmload (%zu byte file) failed (%d); ", clm->size, err);
        /* Retrieve clmload_status and print */
        err = brcmf_fil_iovar_int_get(ifp, "clmload_status", &status);
        if (err != ZX_OK) {
            brcmf_err("get clmload_status failed (%d)\n", err);
        } else {
            brcmf_dbg(INFO, "clmload_status=%d\n", status);
        }
        err = ZX_ERR_IO;
    }

    free(chunk_buf);
done:
    return err;
}

zx_status_t brcmf_c_preinit_dcmds(struct brcmf_if* ifp) {
    int8_t eventmask[BRCMF_EVENTING_MASK_LEN];
    uint8_t buf[BRCMF_DCMD_SMLEN];
    struct brcmf_rev_info_le revinfo;
    struct brcmf_rev_info* ri;
    char* clmver;
    char* ptr;
    zx_status_t err;

    /* retreive mac address */
    err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", ifp->mac_addr, sizeof(ifp->mac_addr));
    if (err != ZX_OK) {
        brcmf_err("Retrieving cur_etheraddr failed, %d\n", err);
        goto done;
    }
    memcpy(ifp->drvr->mac, ifp->mac_addr, sizeof(ifp->drvr->mac));

    err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_REVINFO, &revinfo, sizeof(revinfo));
    ri = &ifp->drvr->revinfo;
    if (err != ZX_OK) {
        brcmf_err("retrieving revision info failed, %d\n", err);
    } else {
        ri->vendorid = revinfo.vendorid;
        ri->deviceid = revinfo.deviceid;
        ri->radiorev = revinfo.radiorev;
        ri->chiprev = revinfo.chiprev;
        ri->corerev = revinfo.corerev;
        ri->boardid = revinfo.boardid;
        ri->boardvendor = revinfo.boardvendor;
        ri->boardrev = revinfo.boardrev;
        ri->driverrev = revinfo.driverrev;
        ri->ucoderev = revinfo.ucoderev;
        ri->bus = revinfo.bus;
        ri->chipnum = revinfo.chipnum;
        ri->phytype = revinfo.phytype;
        ri->phyrev = revinfo.phyrev;
        ri->anarev = revinfo.anarev;
        ri->chippkg = revinfo.chippkg;
        ri->nvramrev = revinfo.nvramrev;
    }
    ri->result = err;

    /* Do any CLM downloading */
    err = brcmf_c_process_clm_blob(ifp);
    if (err != ZX_OK) {
        brcmf_err("download CLM blob file failed, %d\n", err);
        goto done;
    }

    /* query for 'ver' to get version info from firmware */
    memset(buf, 0, sizeof(buf));
    strcpy((char*)buf, "ver");
    err = brcmf_fil_iovar_data_get(ifp, "ver", buf, sizeof(buf));
    if (err != ZX_OK) {
        brcmf_err("Retreiving version information failed, %d\n", err);
        goto done;
    }
    ptr = (char*)buf;
    strsep(&ptr, "\n");

    /* Print fw version info */
    brcmf_info("Firmware version = %s\n", buf);

    /* locate firmware version number for ethtool */
    ptr = strrchr((char*)buf, ' ') + 1;
    strlcpy(ifp->drvr->fwver, ptr, sizeof(ifp->drvr->fwver));

    /* Query for 'clmver' to get CLM version info from firmware */
    memset(buf, 0, sizeof(buf));
    err = brcmf_fil_iovar_data_get(ifp, "clmver", buf, sizeof(buf));
    if (err != ZX_OK) {
        brcmf_dbg(TRACE, "retrieving clmver failed, %d\n", err);
    } else {
        clmver = (char*)buf;
        /* store CLM version for adding it to revinfo debugfs file */
        memcpy(ifp->drvr->clmver, clmver, sizeof(ifp->drvr->clmver));

        /* Replace all newline/linefeed characters with space
         * character
         */
        clmver[sizeof(buf) - 1] = 0;
        ptr = clmver;
        while ((ptr = strchr(ptr, '\n')) != NULL) {
            *ptr = ' ';
        }

        brcmf_dbg(INFO, "CLM version = %s\n", clmver);
    }

    /* set mpc */
    err = brcmf_fil_iovar_int_set(ifp, "mpc", 1);
    if (err != ZX_OK) {
        brcmf_err("failed setting mpc\n");
        goto done;
    }

    brcmf_c_set_joinpref_default(ifp);

    /* Setup event_msgs, enable E_IF */
    err = brcmf_fil_iovar_data_get(ifp, "event_msgs", eventmask, BRCMF_EVENTING_MASK_LEN);
    if (err != ZX_OK) {
        brcmf_err("Get event_msgs error (%d)\n", err);
        goto done;
    }
    setbit(eventmask, BRCMF_E_IF);
    err = brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmask, BRCMF_EVENTING_MASK_LEN);
    if (err != ZX_OK) {
        brcmf_err("Set event_msgs error (%d)\n", err);
        goto done;
    }

    /* Setup default scan channel time */
    err =
        brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME, BRCMF_DEFAULT_SCAN_CHANNEL_TIME);
    if (err != ZX_OK) {
        brcmf_err("BRCMF_C_SET_SCAN_CHANNEL_TIME error (%d)\n", err);
        goto done;
    }

    /* Setup default scan unassoc time */
    err =
        brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME, BRCMF_DEFAULT_SCAN_UNASSOC_TIME);
    if (err != ZX_OK) {
        brcmf_err("BRCMF_C_SET_SCAN_UNASSOC_TIME error (%d)\n", err);
        goto done;
    }

    /* Enable tx beamforming, errors can be ignored (not supported) */
    (void)brcmf_fil_iovar_int_set(ifp, "txbf", 1);

    /* do bus specific preinit here */
    err = brcmf_bus_preinit(ifp->drvr->bus_if);
done:
    return err;
}

#ifndef CONFIG_BRCM_TRACING
void __brcmf_err(const char* func, const char* fmt, ...) {
    struct va_format vaf;
    va_list args;

    va_start(args, fmt);

    vaf.fmt = fmt;
    vaf.va = &args;
    zxlogf(ERROR, "brcmfmac: %s: %pV", func, &vaf);

    va_end(args);
}
#endif

#if defined(CONFIG_BRCM_TRACING) || defined(CONFIG_BRCMDBG)
void __brcmf_dbg(uint32_t filter, const char* func, const char* fmt, ...) {
    va_list args;

    if (true || brcmf_msg_filter & filter) {
        // TODO(cphoenix): After bringup: Re-enable filter check
        char msg[512]; // Same value hard-coded throughout devhost.c
        va_start(args, fmt);
        int n_printed = vsnprintf(msg, 512, fmt, args);
        va_end(args);
        if (n_printed < 0) {
            snprintf(msg, 512, "(Formatting error from string '%s')", fmt);
        } else if (n_printed > 0 && msg[n_printed - 1] == '\n') {
            msg[--n_printed] = 0;
        }
        zxlogf(INFO, "brcmfmac (%s): '%s'\n", func, msg);
    }
}
#endif

static void brcmf_mp_attach(void) {
    /* If module param firmware path is set then this will always be used,
     * if not set then if available use the platform data version. To make
     * sure it gets initialized at all, always copy the module param version
     */
    strlcpy(brcmf_mp_global.firmware_path, brcmf_firmware_path, BRCMF_FW_ALTPATH_LEN);
}

struct brcmf_mp_device* brcmf_get_module_param(struct brcmf_device* dev,
                                               enum brcmf_bus_type bus_type,
                                               uint32_t chip, uint32_t chiprev) {
    struct brcmf_mp_device* settings;

    brcmf_dbg(TEMP, "Enter, bus=%d, chip=%d, rev=%d\n", bus_type, chip, chiprev);
    settings = calloc(1, sizeof(*settings));
    if (!settings) {
        return NULL;
    }

    /* start by using the module paramaters */
    settings->p2p_enable = !!brcmf_p2p_enable;
    settings->feature_disable = brcmf_feature_disable;
    settings->fcmode = brcmf_fcmode;
    settings->roamoff = !!brcmf_roamoff;
#ifdef DEBUG
    settings->ignore_probe_fail = !!brcmf_ignore_probe_fail;
#endif

    if (bus_type == BRCMF_BUSTYPE_SDIO) {
        settings->bus.sdio.txglomsz = brcmf_sdiod_txglomsz;
    }
#ifdef USE_PLATFORM_DATA
// TODO(NET-831): Do we need to do this?
struct brcmfmac_pd_device {
    uint32_t bus_type;
    uint32_t id;
    int rev;
    struct brcmfmac_pd_cc country_codes[555];
    struct {
        void* sdio;
    } bus;
};

    struct brcmfmac_pd_device* device_pd;
    bool found;
    int i;

    /* See if there is any device specific platform data configured */
    found = false;
    if (brcmfmac_pdata) {
        for (i = 0; i < brcmfmac_pdata->device_count; i++) {
            device_pd = &brcmfmac_pdata->devices[i];
            if ((device_pd->bus_type == bus_type) && (device_pd->id == chip) &&
                    ((device_pd->rev == (int32_t)chiprev) || (device_pd->rev == -1))) {
                brcmf_dbg(INFO, "Platform data for device found\n");
                settings->country_codes = device_pd->country_codes;
                if (device_pd->bus_type == BRCMF_BUSTYPE_SDIO) {
                    memcpy(&settings->bus.sdio, &device_pd->bus.sdio, sizeof(settings->bus.sdio));
                }
                found = true;
                break;
            }
        }
    }
    if (!found) {
        /* No platform data for this device, try OF (Open Firwmare) */
        brcmf_of_probe(dev, bus_type, settings);
    }
#endif /* USE_PLATFORM_DATA */
    return settings;
}

void brcmf_release_module_param(struct brcmf_mp_device* module_param) {
    free(module_param);
}

zx_status_t brcmfmac_module_init(zx_device_t* device) {
    zx_status_t err;

    /* Initialize debug system first */
    brcmf_debugfs_init();

    async_loop_t* async_loop;
    async_loop_config_t async_config;
    memset(&async_config, 0, sizeof(async_config));
    err = async_loop_create(&async_config, &async_loop);
    if (err != ZX_OK) {
        return err;
    }
    err = async_loop_start_thread(async_loop, "async_thread", NULL);
    if (err != ZX_OK) {
        async_loop_destroy(async_loop);
        return err;
    }
    default_async = async_loop_get_dispatcher(async_loop);

    /* Initialize global module paramaters */
    brcmf_mp_attach();

    /* Continue the initialization by registering the different busses */
    err = brcmf_core_init(device);
    if (err != ZX_OK) {
        brcmf_debugfs_exit();
    }

    return err;
}

static void brcmfmac_module_exit(void) {
    brcmf_core_exit();
    if (default_async != NULL) {
        async_loop_destroy(async_loop_from_dispatcher(default_async));
    }
    brcmf_debugfs_exit();
}

module_init(brcmfmac_module_init);
module_exit(brcmfmac_module_exit);
