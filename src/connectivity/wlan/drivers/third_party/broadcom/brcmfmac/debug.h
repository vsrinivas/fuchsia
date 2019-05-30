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

#ifndef BRCMFMAC_DEBUG_H
#define BRCMFMAC_DEBUG_H

#include "linuxisms.h"

// clang-format off

/* message levels */
#define BRCMF_TEMP_VAL    0x00000001
#define BRCMF_TRACE_VAL   0x00000002
#define BRCMF_INFO_VAL    0x00000004
#define BRCMF_DATA_VAL    0x00000008
#define BRCMF_CTL_VAL     0x00000010
#define BRCMF_TIMER_VAL   0x00000020
#define BRCMF_HDRS_VAL    0x00000040
#define BRCMF_BYTES_VAL   0x00000080
#define BRCMF_INTR_VAL    0x00000100
#define BRCMF_GLOM_VAL    0x00000200
#define BRCMF_EVENT_VAL   0x00000400
#define BRCMF_BTA_VAL     0x00000800
#define BRCMF_FIL_VAL     0x00001000
#define BRCMF_USB_VAL     0x00002000
#define BRCMF_SCAN_VAL    0x00004000
#define BRCMF_CONN_VAL    0x00008000
#define BRCMF_BCDC_VAL    0x00010000
#define BRCMF_SDIO_VAL    0x00020000
#define BRCMF_MSGBUF_VAL  0x00040000
#define BRCMF_PCIE_VAL    0x00080000
#define BRCMF_FWCON_VAL   0x00100000
#define BRCMF_SIM_VAL     0x00200000
#define BRCMF_ALL_VAL     0xffffffff

// clang-format on

#if (CONFIG_BRCMFMAC_USB || CONFIG_BRCMFMAC_SDIO || CONFIG_BRCMFMAC_PCIE)
#define BRCMF_LOGF(level, msg, ...) zxlogf(level, msg, ##__VA_ARGS__)
#else
#define BRCMF_LOGF(level, msg, ...)  \
    do {                             \
        printf("%s: ", #level);      \
        printf(msg, ##__VA_ARGS__);  \
    } while (0)
#endif

/* set default print format */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

__PRINTFLIKE(2, 3) void __brcmf_err(const char* func, const char* fmt, ...);
#define brcmf_err(fmt, ...) __brcmf_err(__func__, fmt, ##__VA_ARGS__)

#if defined(DEBUG) || defined(CONFIG_BRCMFMAC_DBG)

__PRINTFLIKE(3, 4) void __brcmf_dbg(uint32_t filter, const char* func, const char* fmt, ...);
#define brcmf_dbg(filter, fmt, ...)                                      \
    do {                                                                 \
        __brcmf_dbg(BRCMF_##filter##_VAL, __func__, fmt, ##__VA_ARGS__); \
    } while (0)

#define THROTTLE(n, event)                        \
    {                                             \
        static std::atomic<unsigned long> times;  \
        if (times.fetch_add(1) <= n) { event; }   \
    }

// clang-format off

#define BRCMF_DATA_ON()  (brcmf_msg_filter & BRCMF_DATA_VAL)
#define BRCMF_CTL_ON()   (brcmf_msg_filter & BRCMF_CTL_VAL)
#define BRCMF_HDRS_ON()  (brcmf_msg_filter & BRCMF_HDRS_VAL)
#define BRCMF_BYTES_ON() (brcmf_msg_filter & BRCMF_BYTES_VAL)
#define BRCMF_GLOM_ON()  (brcmf_msg_filter & BRCMF_GLOM_VAL)
#define BRCMF_EVENT_ON() (brcmf_msg_filter & BRCMF_EVENT_VAL)
#define BRCMF_FIL_ON()   (brcmf_msg_filter & BRCMF_FIL_VAL)
#define BRCMF_FWCON_ON() (brcmf_msg_filter & BRCMF_FWCON_VAL)
#define BRCMF_SCAN_ON()  (brcmf_msg_filter & BRCMF_SCAN_VAL)
#define BRCMF_CONN_ON()  (brcmf_msg_filter & BRCMF_CONN_VAL)
#define BRCMF_INFO_ON()  (brcmf_msg_filter & BRCMF_INFO_VAL)

#else /* defined(DEBUG) || defined(CONFIG_BRCMFMAC_DBG) */

#define brcmf_dbg(level, fmt, ...)

#define BRCMF_DATA_ON()  0
#define BRCMF_CTL_ON()   0
#define BRCMF_HDRS_ON()  0
#define BRCMF_BYTES_ON() 0
#define BRCMF_GLOM_ON()  0
#define BRCMF_EVENT_ON() 0
#define BRCMF_FIL_ON()   0
#define BRCMF_FWCON_ON() 0
#define BRCMF_SCAN_ON()  0
#define BRCMF_CONN_ON()  0
#define BRCMF_INFO_ON()  0

// clang-format on

#endif /* defined(DEBUG) || defined(CONFIG_BRCMFMAC_DBG) */

#define brcmf_dbg_hex_dump(test, data, len, fmt, ...)                \
    do {                                                             \
        if (test) {                                                  \
            BRCMF_LOGF(INFO, "brcmfmac: " fmt, ##__VA_ARGS__);       \
            brcmf_hexdump(data, len);                                \
        }                                                            \
    } while (0)

void brcm_dbg_clear_err();
bool brcm_dbg_has_err();

void brcmf_hexdump(const void* buf, size_t len);

void brcmf_alphadump(const void* buf, size_t len);

extern uint32_t brcmf_msg_filter;

struct brcmf_bus;
struct brcmf_pub;
#ifdef DEBUG
void brcmf_debugfs_init(void);
void brcmf_debugfs_exit(void);
zx_status_t brcmf_debug_attach(struct brcmf_pub* drvr);
void brcmf_debug_detach(struct brcmf_pub* drvr);
zx_handle_t brcmf_debugfs_get_devdir(struct brcmf_pub* drvr);
zx_status_t brcmf_debugfs_add_entry(struct brcmf_pub* drvr, const char* fn,
                                    zx_status_t (*read_fn)(struct seq_file* seq, void* data));
zx_status_t brcmf_debug_create_memdump(struct brcmf_bus* bus, const void* data, size_t len);
#else
static inline void brcmf_debugfs_init(void) {}
static inline void brcmf_debugfs_exit(void) {}
static inline zx_status_t brcmf_debug_attach(struct brcmf_pub* drvr) {
    return ZX_OK;
}
static inline void brcmf_debug_detach(struct brcmf_pub* drvr) {}
static inline zx_status_t brcmf_debugfs_add_entry(struct brcmf_pub* drvr, const char* fn,
                                                  zx_status_t (*read_fn)(struct seq_file* seq,
                                                                         void* data)) {
    return ZX_OK;
}
static inline zx_status_t brcmf_debug_create_memdump(struct brcmf_bus* bus, const void* data,
                                                     size_t len) {
    return ZX_OK;
}
#endif

#endif /* BRCMFMAC_DEBUG_H */
