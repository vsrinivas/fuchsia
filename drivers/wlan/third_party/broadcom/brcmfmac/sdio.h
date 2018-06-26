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

#ifndef BRCMFMAC_SDIO_H
#define BRCMFMAC_SDIO_H

#include <ddk/protocol/sdio.h>

#include "defs.h"
#include "device.h"
#include "firmware.h"
#include "linuxisms.h"
#include "netbuf.h"

#define SDIOD_FBR_SIZE 0x100

/* io_en */
#define SDIO_FUNC_ENABLE_1 0x02
#define SDIO_FUNC_ENABLE_2 0x04

/* io_rdys */
#define SDIO_FUNC_READY_1 0x02
#define SDIO_FUNC_READY_2 0x04

/* intr_status */
#define INTR_STATUS_FUNC1 0x2
#define INTR_STATUS_FUNC2 0x4

/* mask of register map */
#define REG_F0_REG_MASK 0x7FF
#define REG_F1_MISC_MASK 0x1FFFF

/* function 0 vendor specific CCCR registers */

#define SDIO_CCCR_BRCM_CARDCAP 0xf0
#define SDIO_CCCR_BRCM_CARDCAP_CMD14_SUPPORT BIT(1)
#define SDIO_CCCR_BRCM_CARDCAP_CMD14_EXT BIT(2)
#define SDIO_CCCR_BRCM_CARDCAP_CMD_NODEC BIT(3)

/* Interrupt enable bits for each function */
#define SDIO_CCCR_IEN_FUNC0 BIT(0)
#define SDIO_CCCR_IEN_FUNC1 BIT(1)
#define SDIO_CCCR_IEN_FUNC2 BIT(2)

#define SDIO_CCCR_BRCM_CARDCTRL 0xf1
#define SDIO_CCCR_BRCM_CARDCTRL_WLANRESET BIT(1)

#define SDIO_CCCR_BRCM_SEPINT 0xf2
#define SDIO_CCCR_BRCM_SEPINT_MASK BIT(0)
#define SDIO_CCCR_BRCM_SEPINT_OE BIT(1)
#define SDIO_CCCR_BRCM_SEPINT_ACT_HI BIT(2)

/* function 1 miscellaneous registers */

/* sprom command and status */
#define SBSDIO_SPROM_CS 0x10000
/* sprom info register */
#define SBSDIO_SPROM_INFO 0x10001
/* sprom indirect access data byte 0 */
#define SBSDIO_SPROM_DATA_LOW 0x10002
/* sprom indirect access data byte 1 */
#define SBSDIO_SPROM_DATA_HIGH 0x10003
/* sprom indirect access addr byte 0 */
#define SBSDIO_SPROM_ADDR_LOW 0x10004
/* gpio select */
#define SBSDIO_GPIO_SELECT 0x10005
/* gpio output */
#define SBSDIO_GPIO_OUT 0x10006
/* gpio enable */
#define SBSDIO_GPIO_EN 0x10007
/* rev < 7, watermark for sdio device */
#define SBSDIO_WATERMARK 0x10008
/* control busy signal generation */
#define SBSDIO_DEVICE_CTL 0x10009

/* SB Address Window Low (b15) */
#define SBSDIO_FUNC1_SBADDRLOW 0x1000A
/* SB Address Window Mid (b23:b16) */
#define SBSDIO_FUNC1_SBADDRMID 0x1000B
/* SB Address Window High (b31:b24)    */
#define SBSDIO_FUNC1_SBADDRHIGH 0x1000C
/* Frame Control (frame term/abort) */
#define SBSDIO_FUNC1_FRAMECTRL 0x1000D
/* ChipClockCSR (ALP/HT ctl/status) */
#define SBSDIO_FUNC1_CHIPCLKCSR 0x1000E
/* SdioPullUp (on cmd, d0-d2) */
#define SBSDIO_FUNC1_SDIOPULLUP 0x1000F
/* Write Frame Byte Count Low */
#define SBSDIO_FUNC1_WFRAMEBCLO 0x10019
/* Write Frame Byte Count High */
#define SBSDIO_FUNC1_WFRAMEBCHI 0x1001A
/* Read Frame Byte Count Low */
#define SBSDIO_FUNC1_RFRAMEBCLO 0x1001B
/* Read Frame Byte Count High */
#define SBSDIO_FUNC1_RFRAMEBCHI 0x1001C
/* MesBusyCtl (rev 11) */
#define SBSDIO_FUNC1_MESBUSYCTRL 0x1001D
/* Sdio Core Rev 12 */
#define SBSDIO_FUNC1_WAKEUPCTRL 0x1001E
#define SBSDIO_FUNC1_WCTRL_ALPWAIT_MASK 0x1
#define SBSDIO_FUNC1_WCTRL_ALPWAIT_SHIFT 0
#define SBSDIO_FUNC1_WCTRL_HTWAIT_MASK 0x2
#define SBSDIO_FUNC1_WCTRL_HTWAIT_SHIFT 1
#define SBSDIO_FUNC1_SLEEPCSR 0x1001F
#define SBSDIO_FUNC1_SLEEPCSR_KSO_MASK 0x1
#define SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT 0
#define SBSDIO_FUNC1_SLEEPCSR_KSO_EN 1
#define SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK 0x2
#define SBSDIO_FUNC1_SLEEPCSR_DEVON_SHIFT 1

#define SBSDIO_FUNC1_MISC_REG_START 0x10000 /* f1 misc register start */
#define SBSDIO_FUNC1_MISC_REG_LIMIT 0x1001F /* f1 misc register end */

/* function 1 OCP space */

/* sb offset addr is <= 15 bits, 32k */
#define SBSDIO_SB_OFT_ADDR_MASK 0x07FFF
#define SBSDIO_SB_OFT_ADDR_LIMIT 0x08000
/* with b15, maps to 32-bit SB access */
#define SBSDIO_SB_ACCESS_2_4B_FLAG 0x08000

/* Address bits from SBADDR regs */
#define SBSDIO_SBWINDOW_MASK 0xffff8000

#define SDIOH_READ 0  /* Read request */
#define SDIOH_WRITE 1 /* Write request */

#define SDIOH_DATA_FIX 0 /* Fixed addressing */
#define SDIOH_DATA_INC 1 /* Incremental addressing */

/* internal return code */
#define SUCCESS 0
#define ERROR 1

/* Packet alignment for most efficient SDIO (can change based on platform) */
#define BRCMF_SDALIGN (1 << 6)

/* watchdog polling interval */
#define BRCMF_WD_POLL_MSEC (10)

/**
 * enum brcmf_sdiod_state - the state of the bus.
 *
 * @BRCMF_SDIOD_DOWN: Device can be accessed, no DPC.
 * @BRCMF_SDIOD_DATA: Ready for data transfers, DPC enabled.
 * @BRCMF_SDIOD_NOMEDIUM: No medium access to dongle possible.
 */
enum brcmf_sdiod_state { BRCMF_SDIOD_DOWN, BRCMF_SDIOD_DATA, BRCMF_SDIOD_NOMEDIUM };

struct brcmf_sdreg {
    int func;
    int offset;
    int value;
};

struct brcmf_sdio;
struct brcmf_sdiod_freezer;

struct sdio_func {
    uint32_t class;
    uint32_t vendor;
    int cur_blksize;
    int enable_timeout;
    int device;
    struct brcmf_device dev;
    int num;
    struct {
        struct mmc_host* host;
        uint32_t quirks;
        void** sdio_func;
    } * card;
};

struct brcmf_sdio_dev {
    struct sdio_func* func1;
    struct sdio_func* func2;
    sdio_protocol_t* zx_dev;
    uint32_t sbwad;             /* Save backplane window address */
    struct brcmf_core* cc_core; /* chipcommon core info struct */
    struct brcmf_sdio* bus;
    struct brcmf_device* dev;
    struct brcmf_bus* bus_if;
    struct brcmf_mp_device* settings;
    bool oob_irq_requested;
    bool sd_irq_requested;
    bool irq_en; /* irq enable flags */
    //spinlock_t irq_en_lock;
    bool irq_wake; /* irq wake enable flags */
    uint txglomsz;
    char fw_name[BRCMF_FW_NAME_LEN];
    char nvram_name[BRCMF_FW_NAME_LEN];
    bool wowl_enabled;
    enum brcmf_sdiod_state state;
    struct brcmf_sdiod_freezer* freezer;
};

/* sdio core registers */
struct sdpcmd_regs {
    uint32_t corecontrol; /* 0x00, rev8 */
    uint32_t corestatus;  /* rev8 */
    uint32_t PAD[1];
    uint32_t biststatus; /* rev8 */

    /* PCMCIA access */
    uint16_t pcmciamesportaladdr; /* 0x010, rev8 */
    uint16_t PAD[1];
    uint16_t pcmciamesportalmask; /* rev8 */
    uint16_t PAD[1];
    uint16_t pcmciawrframebc; /* rev8 */
    uint16_t PAD[1];
    uint16_t pcmciaunderflowtimer; /* rev8 */
    uint16_t PAD[1];

    /* interrupt */
    uint32_t intstatus;   /* 0x020, rev8 */
    uint32_t hostintmask; /* rev8 */
    uint32_t intmask;     /* rev8 */
    uint32_t sbintstatus; /* rev8 */
    uint32_t sbintmask;   /* rev8 */
    uint32_t funcintmask; /* rev4 */
    uint32_t PAD[2];
    uint32_t tosbmailbox;       /* 0x040, rev8 */
    uint32_t tohostmailbox;     /* rev8 */
    uint32_t tosbmailboxdata;   /* rev8 */
    uint32_t tohostmailboxdata; /* rev8 */

    /* synchronized access to registers in SDIO clock domain */
    uint32_t sdioaccess; /* 0x050, rev8 */
    uint32_t PAD[3];

    /* PCMCIA frame control */
    uint8_t pcmciaframectrl; /* 0x060, rev8 */
    uint8_t PAD[3];
    uint8_t pcmciawatermark; /* rev8 */
    uint8_t PAD[155];

    /* interrupt batching control */
    uint32_t intrcvlazy; /* 0x100, rev8 */
    uint32_t PAD[3];

    /* counters */
    uint32_t cmd52rd;      /* 0x110, rev8 */
    uint32_t cmd52wr;      /* rev8 */
    uint32_t cmd53rd;      /* rev8 */
    uint32_t cmd53wr;      /* rev8 */
    uint32_t abort;        /* rev8 */
    uint32_t datacrcerror; /* rev8 */
    uint32_t rdoutofsync;  /* rev8 */
    uint32_t wroutofsync;  /* rev8 */
    uint32_t writebusy;    /* rev8 */
    uint32_t readwait;     /* rev8 */
    uint32_t readterm;     /* rev8 */
    uint32_t writeterm;    /* rev8 */
    uint32_t PAD[40];
    uint32_t clockctlstatus; /* rev8 */
    uint32_t PAD[7];

    uint32_t PAD[128]; /* DMA engines */

    /* SDIO/PCMCIA CIS region */
    char cis[512]; /* 0x400-0x5ff, rev6 */

    /* PCMCIA function control registers */
    char pcmciafcr[256]; /* 0x600-6ff, rev6 */
    uint16_t PAD[55];

    /* PCMCIA backplane access */
    uint16_t backplanecsr;   /* 0x76E, rev6 */
    uint16_t backplaneaddr0; /* rev6 */
    uint16_t backplaneaddr1; /* rev6 */
    uint16_t backplaneaddr2; /* rev6 */
    uint16_t backplaneaddr3; /* rev6 */
    uint16_t backplanedata0; /* rev6 */
    uint16_t backplanedata1; /* rev6 */
    uint16_t backplanedata2; /* rev6 */
    uint16_t backplanedata3; /* rev6 */
    uint16_t PAD[31];

    /* sprom "size" & "blank" info */
    uint16_t spromstatus; /* 0x7BE, rev2 */
    uint32_t PAD[464];

    uint16_t PAD[0x80];
};

/* Register/deregister interrupt handler. */
zx_status_t brcmf_sdiod_intr_register(struct brcmf_sdio_dev* sdiodev);
void brcmf_sdiod_intr_unregister(struct brcmf_sdio_dev* sdiodev);

/* SDIO device register access interface */
/* Accessors for SDIO Function 0 */
#define brcmf_sdiod_func0_rb(sdiodev, addr, r) sdio_f0_readb((sdiodev)->func1, (addr), (r))

#define brcmf_sdiod_func0_wb(sdiodev, addr, v, ret) \
    sdio_f0_writeb((sdiodev)->func1, (v), (addr), (ret))

/* Accessors for SDIO Function 1 */
#define brcmf_sdiod_readb(sdiodev, addr, r) sdio_readb((sdiodev)->func1, (addr), (r))

#define brcmf_sdiod_writeb(sdiodev, addr, v, ret) sdio_writeb((sdiodev)->func1, (v), (addr), (ret))

uint32_t brcmf_sdiod_readl(struct brcmf_sdio_dev* sdiodev, uint32_t addr, zx_status_t* ret);
void brcmf_sdiod_writel(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint32_t data,
                        zx_status_t* ret);

/* Buffer transfer to/from device (client) core via cmd53.
 *   fn:       function number
 *   flags:    backplane width, address increment, sync/async
 *   buf:      pointer to memory data buffer
 *   nbytes:   number of bytes to transfer to/from buf
 *   pkt:      pointer to packet associated with buf (if any)
 *   complete: callback function for command completion (async only)
 *   handle:   handle for completion callback (first arg in callback)
 * Returns 0 or error code.
 * NOTE: Async operation is not currently supported.
 */
zx_status_t brcmf_sdiod_send_pkt(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf_list* pktq);
zx_status_t brcmf_sdiod_send_buf(struct brcmf_sdio_dev* sdiodev, uint8_t* buf, uint nbytes);

zx_status_t brcmf_sdiod_recv_pkt(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf* pkt);
zx_status_t brcmf_sdiod_recv_buf(struct brcmf_sdio_dev* sdiodev, uint8_t* buf, uint nbytes);
zx_status_t brcmf_sdiod_recv_chain(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf_list* pktq,
                                   uint totlen);

/* Flags bits */

/* Four-byte target (backplane) width (vs. two-byte) */
#define SDIO_REQ_4BYTE 0x1
/* Fixed address (FIFO) (vs. incrementing address) */
#define SDIO_REQ_FIXED 0x2

/* Read/write to memory block (F1, no FIFO) via CMD53 (sync only).
 *   rw:       read or write (0/1)
 *   addr:     direct SDIO address
 *   buf:      pointer to memory data buffer
 *   nbytes:   number of bytes to transfer to/from buf
 * Returns 0 or error code.
 */
zx_status_t brcmf_sdiod_ramrw(struct brcmf_sdio_dev* sdiodev, bool write, uint32_t address,
                              uint8_t* data, uint size);
// TODO(cphoenix): Expand "uint" to "unsigned int" everywhere.

/* Issue an abort to the specified function */
int brcmf_sdiod_abort(struct brcmf_sdio_dev* sdiodev, struct sdio_func* func);

void brcmf_sdiod_change_state(struct brcmf_sdio_dev* sdiodev, enum brcmf_sdiod_state state);
#ifdef CONFIG_PM_SLEEP
bool brcmf_sdiod_freezing(struct brcmf_sdio_dev* sdiodev);
void brcmf_sdiod_try_freeze(struct brcmf_sdio_dev* sdiodev);
void brcmf_sdiod_freezer_count(struct brcmf_sdio_dev* sdiodev);
void brcmf_sdiod_freezer_uncount(struct brcmf_sdio_dev* sdiodev);
#else
static inline bool brcmf_sdiod_freezing(struct brcmf_sdio_dev* sdiodev) {
    return false;
}
static inline void brcmf_sdiod_try_freeze(struct brcmf_sdio_dev* sdiodev) {}
static inline void brcmf_sdiod_freezer_count(struct brcmf_sdio_dev* sdiodev) {}
static inline void brcmf_sdiod_freezer_uncount(struct brcmf_sdio_dev* sdiodev) {}
#endif /* CONFIG_PM_SLEEP */

struct brcmf_sdio* brcmf_sdio_probe(struct brcmf_sdio_dev* sdiodev);
void brcmf_sdio_remove(struct brcmf_sdio* bus);
void brcmf_sdio_isr(struct brcmf_sdio* bus);

void brcmf_sdio_wd_timer(struct brcmf_sdio* bus, bool active);
void brcmf_sdio_wowl_config(struct brcmf_device* dev, bool enabled);
zx_status_t brcmf_sdio_sleep(struct brcmf_sdio* bus, bool sleep);
void brcmf_sdio_trigger_dpc(struct brcmf_sdio* bus);

#endif /* BRCMFMAC_SDIO_H */
