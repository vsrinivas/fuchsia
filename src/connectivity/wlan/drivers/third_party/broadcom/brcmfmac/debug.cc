/*
 * Copyright (c) 2012 Broadcom Corporation
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

#include "debug.h"

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "core.h"
#include "device.h"
#include "linuxisms.h"
#include "fweh.h"

#define BRCMF_HEXDUMP_WIDTH 16

// Debug level configuration. See debug.h for bits (BRCMF_*_VAL)
uint32_t brcmf_msg_filter;

static zx_handle_t root_folder;

bool __brcm_dbg_err_flag = false;
void __brcm_dbg_set_err() { __brcm_dbg_err_flag = true; }
bool brcm_dbg_has_err() { return __brcm_dbg_err_flag; }
void brcm_dbg_clear_err() { __brcm_dbg_err_flag = false; }

#if CONFIG_BRCMFMAC_DBG

void __brcmf_dbg(uint32_t filter, const char* func, const char* fmt, ...) {
    if (brcmf_msg_filter & filter) {
        char msg[512]; // Same value hard-coded throughout devhost.c
        va_list args;
        va_start(args, fmt);
        int n_printed = vsnprintf(msg, 512, fmt, args);
        va_end(args);
        if (n_printed < 0) {
            snprintf(msg, 512, "(Formatting error from string '%s')", fmt);
        } else if (n_printed > 0 && msg[n_printed - 1] == '\n') {
            msg[--n_printed] = 0;
        }
        zxlogf(INFO, "brcmfmac (%s): %s\n", func, msg);
    }
}

#else

void __brcmf_dbg(uint32_t filter, const char* fucn, const char* fmt, ...) {}

#endif  // CONFIG_BRCMFMAC_DBG

void __brcmf_err(const char* func, const char* fmt, ...) {
    char msg[512]; // Same value hard-coded throughout devhost.c
    va_list args;
    va_start(args, fmt);
    int n_printed = vsnprintf(msg, 512, fmt, args);
    va_end(args);
    if (n_printed < 0) {
        snprintf(msg, 512, "(Formatting error from string '%s')", fmt);
    }
    __brcm_dbg_set_err();
    zxlogf(ERROR, "brcmfmac (%s): %s", func, msg);
}

void brcmf_hexdump(const void* buf, size_t len) {
    if (len > 4096) {
        zxlogf(INFO, "brcmfmac: Truncating hexdump to 4096 bytes\n");
        len = 4096;
    }
    if (len == 0) {
        zxlogf(INFO, "brcmfmac: Empty hexdump %p\n", buf);
        return;
    }
    char output[64 + BRCMF_HEXDUMP_WIDTH * 3];
    uint8_t* bytes = (uint8_t*)buf;
    size_t i;
    char* next = output;
    for (i = 0; i < len; i++) {
        next += sprintf(next, "%02x ", bytes[i % BRCMF_HEXDUMP_WIDTH]);
        if ((i % BRCMF_HEXDUMP_WIDTH) == (BRCMF_HEXDUMP_WIDTH - 1)) {
            // For larger dumps over serial, a delay may need to be added
            zxlogf(INFO, "%p: %s\n", bytes, output);
            next = output;
            bytes += BRCMF_HEXDUMP_WIDTH;
        }
    }
    if ((i % BRCMF_HEXDUMP_WIDTH) != 0) {
        zxlogf(INFO, "%p: %s\n", bytes, output);
    }
}

void brcmf_alphadump(const void* buf, size_t len) {
    if (len == 0) {
        brcmf_dbg(INFO, "Empty alphadump %p", buf);
        return;
    }
    char output[140];
    uint8_t* bytes = (uint8_t*)buf;
    size_t i;
    int nonprinting = 0;
    char* next = output;
    bool overflow = false;
    next += sprintf(next, "Alpha: \"");
    for (i = 0; i < len; i++) {
        if (bytes[i] >= 32 && bytes[i] < 128) {
            if (nonprinting) {
                next += sprintf(next, ",%d.", nonprinting);
                nonprinting = 0;
            }
            next += sprintf(next, "%c", bytes[i]);
        } else {
            nonprinting++;
        }
        if (next > output + 125) {
            overflow = true;
            break;
        }
    }
    if (nonprinting) {
        next += sprintf(next, ",%d.", nonprinting);
        nonprinting = 0;
    }
    if (overflow) {
        next += sprintf(next, ">etc<");
    }
    sprintf(next, "\"\n");
    brcmf_dbg(ALL, "%s", output);
}

zx_status_t brcmf_debug_create_memdump(struct brcmf_bus* bus, const void* data, size_t len) {
    uint8_t* dump;
    size_t ramsize;
    zx_status_t err;

    ramsize = brcmf_bus_get_ramsize(bus);
    if (!ramsize) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    dump = static_cast<decltype(dump)>(calloc(1, len + ramsize));
    if (!dump) {
        return ZX_ERR_NO_MEMORY;
    }

    memcpy(dump, data, len);
    err = brcmf_bus_get_memdump(bus, dump + len, ramsize);
    if (err != ZX_OK) {
        free(dump);
        return err;
    }

    dev_coredumpv(bus->dev, dump, len + ramsize);

    return ZX_OK;
}

void brcmf_debugfs_init(void) {
    zx_status_t err;

    err = brcmf_debugfs_create_directory(KBUILD_MODNAME, ZX_HANDLE_INVALID, &root_folder);
    if (err != ZX_OK) {
        root_folder = ZX_HANDLE_INVALID;
    }
}

void brcmf_debugfs_exit(void) {
    if (root_folder == ZX_HANDLE_INVALID) {
        return;
    }

    brcmf_debugfs_rm_recursive(root_folder);
    root_folder = ZX_HANDLE_INVALID;
}

zx_status_t brcmf_debug_attach(struct brcmf_pub* drvr) {
    struct brcmf_device* dev = drvr->bus_if->dev;
    zx_status_t ret;

    if (root_folder == ZX_HANDLE_INVALID) {
        return ZX_ERR_NOT_FILE;
    }

    ret = brcmf_debugfs_create_directory(device_get_name(dev->zxdev), root_folder,
                                         &drvr->dbgfs_dir);
    return ret;
}

void brcmf_debug_detach(struct brcmf_pub* drvr) {
    brcmf_fweh_unregister(drvr, BRCMF_E_PSM_WATCHDOG);

    if (drvr->dbgfs_dir != ZX_HANDLE_INVALID) {
        brcmf_debugfs_rm_recursive(drvr->dbgfs_dir);
    }
}

zx_handle_t brcmf_debugfs_get_devdir(struct brcmf_pub* drvr) {
    return drvr->dbgfs_dir;
}

zx_status_t brcmf_debugfs_add_entry(struct brcmf_pub* drvr, const char* fn,
                                    zx_status_t (*read_fn)(struct seq_file* seq, void* data)) {
    zx_handle_t e;
    zx_status_t ret;

    ret = brcmf_debugfs_create_sequential_file(drvr->bus_if->dev, fn, drvr->dbgfs_dir, read_fn, &e);
    return ret;
}
