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

#include "sdio.h"

#include <lib/sync/completion.h>
#include <zircon/status.h>

#include <stdatomic.h>
#include <threads.h>

#include "brcm_hw_ids.h"
#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bcdc.h"
#include "chip.h"
#include "common.h"
#include "core.h"
#include "defs.h"
#include "device.h"
#include "firmware.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "soc.h"
#include "workqueue.h"

#define DCMD_RESP_TIMEOUT_MSEC (2500)
#define CTL_DONE_TIMEOUT_MSEC (2500)

#ifdef DEBUG

#define BRCMF_TRAP_INFO_SIZE 80

#define CBUF_LEN (128)

/* Device console log buffer state */
#define CONSOLE_BUFFER_MAX 2024

struct rte_log_le {
    uint32_t buf; /* Can't be pointer on (64-bit) hosts */
    uint32_t buf_size;
    uint32_t idx;
    char* _buf_compat; /* Redundant pointer for backward compat. */
};

struct rte_console {
    /* Virtual UART
     * When there is no UART (e.g. Quickturn),
     * the host should write a complete
     * input line directly into cbuf and then write
     * the length into vcons_in.
     * This may also be used when there is a real UART
     * (at risk of conflicting with
     * the real UART).  vcons_out is currently unused.
     */
    uint vcons_in;
    uint vcons_out;

    /* Output (logging) buffer
     * Console output is written to a ring buffer log_buf at index log_idx.
     * The host may read the output when it sees log_idx advance.
     * Output will be lost if the output wraps around faster than the host
     * polls.
     */
    struct rte_log_le log_le;

    /* Console input line buffer
     * Characters are read one at a time into cbuf
     * until <CR> is received, then
     * the buffer is processed as a command line.
     * Also used for virtual UART.
     */
    uint cbuf_idx;
    char cbuf[CBUF_LEN];
};

#endif /* DEBUG */
#include "chipcommon.h"

#include "bus.h"
#include "debug.h"
#include "device.h"
#include "tracepoint.h"

#define TXQLEN 2048         /* bulk tx queue length */
#define TXHI (TXQLEN - 256) /* turn on flow control above TXHI */
#define TXLOW (TXHI - 256)  /* turn off flow control below TXLOW */
#define PRIOMASK 7

#define TXRETRIES 2 /* # of retries for tx frames */

/* Default for max rx frames in
   one scheduling */
#define BRCMF_RXBOUND 50

/* Default for max tx frames in
   one scheduling */
#define BRCMF_TXBOUND 20

#define BRCMF_TXMINMAX 1 /* Max tx frames if rx still pending */

/* Block size used for downloading
   of dongle image */
#define MEMBLOCK 2048
/* Must be large enough to hold
   biggest possible glom */
#define MAX_DATA_BUF (32 * 1024)

#define BRCMF_FIRSTREAD (1 << 6)

#define BRCMF_CONSOLE 10 /* watchdog interval to poll console */

/* SBSDIO_DEVICE_CTL */

// clang-format off

/* 1: device will assert busy signal when receiving CMD53 */
#define SBSDIO_DEVCTL_SETBUSY       0x01
/* 1: assertion of sdio interrupt is synchronous to the sdio clock */
#define SBSDIO_DEVCTL_SPI_INTR_SYNC 0x02
/* 1: mask all interrupts to host except the chipActive (rev 8) */
#define SBSDIO_DEVCTL_CA_INT_ONLY   0x04
/* 1: isolate internal sdio signals, put external pads in tri-state; requires
 * sdio bus power cycle to clear (rev 9) */
#define SBSDIO_DEVCTL_PADS_ISO      0x08
/* Force SD->SB reset mapping (rev 11) */
#define SBSDIO_DEVCTL_SB_RST_CTL    0x30
/*   Determined by CoreControl bit */
#define SBSDIO_DEVCTL_RST_CORECTL   0x00
/*   Force backplane reset */
#define SBSDIO_DEVCTL_RST_BPRESET   0x10
/*   Force no backplane reset */
#define SBSDIO_DEVCTL_RST_NOBPRESET 0x20

/* direct(mapped) cis space */

/* MAPPED common CIS address */
#define SBSDIO_CIS_BASE_COMMON 0x1000
/* maximum bytes in one CIS */
#define SBSDIO_CIS_SIZE_LIMIT 0x200
/* cis offset addr is < 17 bits */
#define SBSDIO_CIS_OFT_ADDR_MASK 0x1FFFF

/* manfid tuple length, include tuple, link bytes */
#define SBSDIO_CIS_MANFID_TUPLE_LEN 6

#define SD_REG(field) (offsetof(struct sdpcmd_regs, field))

/* SDIO function 1 register CHIPCLKCSR */
/* Force ALP request to backplane */
#define SBSDIO_FORCE_ALP           0x01
/* Force HT request to backplane */
#define SBSDIO_FORCE_HT            0x02
/* Force ILP request to backplane */
#define SBSDIO_FORCE_ILP           0x04
/* Make ALP ready (power up xtal) */
#define SBSDIO_ALP_AVAIL_REQ       0x08
/* Make HT ready (power up PLL) */
#define SBSDIO_HT_AVAIL_REQ        0x10
/* Squelch clock requests from HW */
#define SBSDIO_FORCE_HW_CLKREQ_OFF 0x20
/* Status: ALP is ready */
#define SBSDIO_ALP_AVAIL           0x40
/* Status: HT is ready */
#define SBSDIO_HT_AVAIL            0x80
#define SBSDIO_CSR_MASK 0x1F
#define SBSDIO_AVBITS (SBSDIO_HT_AVAIL | SBSDIO_ALP_AVAIL)
#define SBSDIO_ALPAV(regval) ((regval)&SBSDIO_AVBITS)
#define SBSDIO_HTAV(regval) (((regval)&SBSDIO_AVBITS) == SBSDIO_AVBITS)
#define SBSDIO_ALPONLY(regval) (SBSDIO_ALPAV(regval) && !SBSDIO_HTAV(regval))
#define SBSDIO_CLKAV(regval, alponly) (SBSDIO_ALPAV(regval) && (alponly ? 1 : SBSDIO_HTAV(regval)))

/* intstatus */
#define I_SMB_SW0 (1 << 0)        /* To SB Mail S/W interrupt 0 */
#define I_SMB_SW1 (1 << 1)        /* To SB Mail S/W interrupt 1 */
#define I_SMB_SW2 (1 << 2)        /* To SB Mail S/W interrupt 2 */
#define I_SMB_SW3 (1 << 3)        /* To SB Mail S/W interrupt 3 */
#define I_SMB_SW_MASK 0x0000000f  /* To SB Mail S/W interrupts mask */
#define I_SMB_SW_SHIFT 0          /* To SB Mail S/W interrupts shift */
#define I_HMB_SW0 (1 << 4)        /* To Host Mail S/W interrupt 0 */
#define I_HMB_SW1 (1 << 5)        /* To Host Mail S/W interrupt 1 */
#define I_HMB_SW2 (1 << 6)        /* To Host Mail S/W interrupt 2 */
#define I_HMB_SW3 (1 << 7)        /* To Host Mail S/W interrupt 3 */
#define I_HMB_SW_MASK 0x000000f0  /* To Host Mail S/W interrupts mask */
#define I_HMB_SW_SHIFT 4          /* To Host Mail S/W interrupts shift */
#define I_WR_OOSYNC (1 << 8)      /* Write Frame Out Of Sync */
#define I_RD_OOSYNC (1 << 9)      /* Read Frame Out Of Sync */
#define I_PC (1 << 10)            /* descriptor error */
#define I_PD (1 << 11)            /* data error */
#define I_DE (1 << 12)            /* Descriptor protocol Error */
#define I_RU (1 << 13)            /* Receive descriptor Underflow */
#define I_RO (1 << 14)            /* Receive fifo Overflow */
#define I_XU (1 << 15)            /* Transmit fifo Underflow */
#define I_RI (1 << 16)            /* Receive Interrupt */
#define I_BUSPWR (1 << 17)        /* SDIO Bus Power Change (rev 9) */
#define I_XMTDATA_AVAIL (1 << 23) /* bits in fifo */
#define I_XI (1 << 24)            /* Transmit Interrupt */
#define I_RF_TERM (1 << 25)       /* Read Frame Terminate */
#define I_WF_TERM (1 << 26)       /* Write Frame Terminate */
#define I_PCMCIA_XU (1 << 27)     /* PCMCIA Transmit FIFO Underflow */
#define I_SBINT (1 << 28)         /* sbintstatus Interrupt */
#define I_CHIPACTIVE (1 << 29)    /* chip from doze to active state */
#define I_SRESET (1 << 30)        /* CCCR RES interrupt */
#define I_IOE2 (1U << 31)         /* CCCR IOE2 Bit Changed */
#define I_ERRORS (I_PC | I_PD | I_DE | I_RU | I_RO | I_XU)
#define I_DMA (I_RI | I_XI | I_ERRORS)

/* corecontrol */
#define CC_CISRDY            (1 << 0)     /* CIS Ready */
#define CC_BPRESEN           (1 << 1)     /* CCCR RES signal */
#define CC_F2RDY             (1 << 2)     /* set CCCR IOR2 bit */
#define CC_CLRPADSISO        (1 << 3)     /* clear SDIO pads isolation */
#define CC_XMTDATAAVAIL_MODE (1 << 4)
#define CC_XMTDATAAVAIL_CTRL (1 << 5)

/* SDA_FRAMECTRL */
#define SFC_RF_TERM  (1 << 0)  /* Read Frame Terminate */
#define SFC_WF_TERM  (1 << 1)  /* Write Frame Terminate */
#define SFC_CRC4WOOS (1 << 2)  /* CRC error for write out of sync */
#define SFC_ABORTALL (1 << 3)  /* Abort all in-progress frames */

/*
 * Software allocation of To SB Mailbox resources
 */

/* tosbmailbox bits corresponding to intstatus bits */
#define SMB_NAK     (1 << 0)   /* Frame NAK */
#define SMB_INT_ACK (1 << 1)   /* Host Interrupt ACK */
#define SMB_USE_OOB (1 << 2)   /* Use OOB Wakeup */
#define SMB_DEV_INT (1 << 3)   /* Miscellaneous Interrupt */

/* tosbmailboxdata */
#define SMB_DATA_VERSION_SHIFT 16 /* host protocol version */

/*
 * Software allocation of To Host Mailbox resources
 */

/* intstatus bits */
#define I_HMB_FC_STATE I_HMB_SW0  /* Flow Control State */
#define I_HMB_FC_CHANGE I_HMB_SW1 /* Flow Control State Changed */
#define I_HMB_FRAME_IND I_HMB_SW2 /* Frame Indication */
#define I_HMB_HOST_INT I_HMB_SW3  /* Miscellaneous Interrupt */

/* tohostmailboxdata */
#define HMB_DATA_NAKHANDLED 0x0001   /* retransmit NAK'd frame */
#define HMB_DATA_DEVREADY   0x0002   /* talk to host after enable */
#define HMB_DATA_FC         0x0004   /* per prio flowcontrol update flag */
#define HMB_DATA_FWREADY    0x0008   /* fw ready for protocol activity */
#define HMB_DATA_FWHALT     0x0010   /* firmware halted */

#define HMB_DATA_FCDATA_MASK   0xff000000
#define HMB_DATA_FCDATA_SHIFT  24

#define HMB_DATA_VERSION_MASK  0x00ff0000
#define HMB_DATA_VERSION_SHIFT 16

/*
 * Software-defined protocol header
 */

/* Current protocol version */
#define SDPCM_PROT_VERSION 4

/*
 * Shared structure between dongle and the host.
 * The structure contains pointers to trap or assert information.
 */
#define SDPCM_SHARED_VERSION       0x0003
#define SDPCM_SHARED_VERSION_MASK  0x00FF
#define SDPCM_SHARED_ASSERT_BUILT  0x0100
#define SDPCM_SHARED_ASSERT        0x0200
#define SDPCM_SHARED_TRAP          0x0400

/* Space for header read, limit for data packets */
#define MAX_HDR_READ  (1 << 6)
#define MAX_RX_DATASZ 2048

// clang-format on

/* Bump up limit on waiting for HT to account for first startup;
 * if the image is doing a CRC calculation before programming the PMU
 * for HT availability, it could take a couple hundred ms more, so
 * max out at a 1 second (1000000us).
 */
#undef PMU_MAX_TRANSITION_DLY_USEC
#define PMU_MAX_TRANSITION_DLY_USEC 1000000

/* Value for ChipClockCSR during initial setup */
#define BRCMF_INIT_CLKCTL1 (SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ)

/* Flags for SDH calls */
#define F2SYNC (SDIO_REQ_4BYTE | SDIO_REQ_FIXED)

#define BRCMF_IDLE_ACTIVE 0  /* Do not request any SD clock change when idle */
#define BRCMF_IDLE_INTERVAL 1

#define KSO_WAIT_US 50
#define MAX_KSO_ATTEMPTS (PMU_MAX_TRANSITION_DLY_USEC / KSO_WAIT_US)
#define BRCMF_SDIO_MAX_ACCESS_ERRORS 5

/*
 * Conversion of 802.1D priority to precedence level
 */
static uint prio2prec(uint32_t prio) {
    return (prio == PRIO_8021D_NONE || prio == PRIO_8021D_BE) ? (prio ^ 2) : prio;
}

#ifdef DEBUG
/* Device console log buffer state */
struct brcmf_console {
    uint count;               /* Poll interval msec counter */
    uint log_addr;            /* Log struct address (fixed) */
    struct rte_log_le log_le; /* Log struct (host copy) */
    uint bufsize;             /* Size of log buffer */
    uint8_t* buf;                  /* Log buffer (host copy) */
    uint last;                /* Last buffer read index */
};

struct brcmf_trap_info {
    uint32_t type;
    uint32_t epc;
    uint32_t cpsr;
    uint32_t spsr;
    uint32_t r0;  /* a1 */
    uint32_t r1;  /* a2 */
    uint32_t r2;  /* a3 */
    uint32_t r3;  /* a4 */
    uint32_t r4;  /* v1 */
    uint32_t r5;  /* v2 */
    uint32_t r6;  /* v3 */
    uint32_t r7;  /* v4 */
    uint32_t r8;  /* v5 */
    uint32_t r9;  /* sb/v6 */
    uint32_t r10; /* sl/v7 */
    uint32_t r11; /* fp/v8 */
    uint32_t r12; /* ip */
    uint32_t r13; /* sp */
    uint32_t r14; /* lr */
    uint32_t pc;  /* r15 */
};
#endif /* DEBUG */

struct sdpcm_shared {
    uint32_t flags;
    uint32_t trap_addr;
    uint32_t assert_exp_addr;
    uint32_t assert_file_addr;
    uint32_t assert_line;
    uint32_t console_addr; /* Address of struct rte_console */
    uint32_t msgtrace_addr;
    uint8_t tag[32];
    uint32_t brpt_addr;
};

struct sdpcm_shared_le {
    uint32_t flags;
    uint32_t trap_addr;
    uint32_t assert_exp_addr;
    uint32_t assert_file_addr;
    uint32_t assert_line;
    uint32_t console_addr; /* Address of struct rte_console */
    uint32_t msgtrace_addr;
    uint8_t tag[32];
    uint32_t brpt_addr;
};

/* dongle SDIO bus specific header info */
struct brcmf_sdio_hdrinfo {
    uint8_t seq_num;
    uint8_t channel;
    uint16_t len;
    uint16_t len_left;
    uint16_t len_nxtfrm;
    uint8_t dat_offset;
    bool lastfrm;
    uint16_t tail_pad;
};

/*
 * hold counter variables
 */
struct brcmf_sdio_count {
    uint intrcount;         /* Count of device interrupt callbacks */
    uint lastintrs;         /* Count as of last watchdog timer */
    uint pollcnt;           /* Count of active polls */
    uint regfails;          /* Count of R_REG failures */
    uint tx_sderrs;         /* Count of tx attempts with sd errors */
    uint fcqueued;          /* Tx packets that got queued */
    uint rxrtx;             /* Count of rtx requests (NAK to dongle) */
    uint rx_toolong;        /* Receive frames too long to receive */
    uint rxc_errors;        /* SDIO errors when reading control frames */
    uint rx_hdrfail;        /* SDIO errors on header reads */
    uint rx_badhdr;         /* Bad received headers (roosync?) */
    uint rx_badseq;         /* Mismatched rx sequence number */
    uint fc_rcvd;           /* Number of flow-control events received */
    uint fc_xoff;           /* Number which turned on flow-control */
    uint fc_xon;            /* Number which turned off flow-control */
    uint rxglomfail;        /* Failed deglom attempts */
    uint rxglomframes;      /* Number of glom frames (superframes) */
    uint rxglompkts;        /* Number of packets from glom frames */
    uint f2rxhdrs;          /* Number of header reads */
    uint f2rxdata;          /* Number of frame data reads */
    uint f2txdata;          /* Number of f2 frame writes */
    uint f1regdata;         /* Number of f1 register accesses */
    uint tickcnt;           /* Number of watchdog been schedule */
    ulong tx_ctlerrs;       /* Err of sending ctrl frames */
    ulong tx_ctlpkts;       /* Ctrl frames sent to dongle */
    ulong rx_ctlerrs;       /* Err of processing rx ctrl frames */
    ulong rx_ctlpkts;       /* Ctrl frames processed from dongle */
    ulong rx_readahead_cnt; /* packets where header read-ahead was used */
};

/* misc chip info needed by some of the routines */
/* Private data for SDIO bus interaction */
struct brcmf_sdio {
    struct brcmf_sdio_dev* sdiodev; /* sdio device handler */
    struct brcmf_chip* ci;          /* Chip info struct */
    struct brcmf_core* sdio_core;   /* sdio core info struct */

    uint32_t hostintmask;    /* Copy of Host Interrupt Mask */
    atomic_int intstatus; /* Intstatus bits (events) pending */
    atomic_int fcstate;   /* State of dongle flow-control */

    uint16_t blocksize; /* Block size of SDIO transfers */
    uint roundup;   /* Max roundup limit */

    struct pktq txq; /* Queue length used for flow-control */
    uint8_t flowcontrol;  /* per prio flow control bitmask */
    uint8_t tx_seq;       /* Transmit sequence number (next) */
    uint8_t tx_max;       /* Maximum transmit sequence allowed */

    uint8_t* hdrbuf; /* buffer for handling rx frame */
    uint8_t* rxhdr;  /* Header of current rx frame (in hdrbuf) */
    uint8_t rx_seq;  /* Receive sequence number (expected) */
    struct brcmf_sdio_hdrinfo cur_read;
    /* info of current read frame */
    bool rxskip;    /* Skip receive (awaiting NAK ACK) */
    bool rxpending; /* Data frame pending in dongle */

    uint rxbound; /* Rx frames to read before resched */
    uint txbound; /* Tx frames to send before resched */
    uint txminmax;

    struct brcmf_netbuf* glomd;    /* Packet containing glomming descriptor */
    struct brcmf_netbuf_list glom; /* Packet list for glommed superframe */

    uint8_t* rxbuf;             /* Buffer for receiving control packets */
    uint rxblen;           /* Allocated length of rxbuf */
    uint8_t* rxctl;             /* Aligned pointer into rxbuf */
    uint8_t* rxctl_orig;        /* pointer for freeing rxctl */
    uint rxlen;            /* Length of valid data in buffer */
    //spinlock_t rxctl_lock; /* protection lock for ctrl frame resources */

    uint8_t sdpcm_ver; /* Bus protocol reported by dongle */

    bool intr;      /* Use interrupts */
    bool poll;      /* Use polling */
    atomic_int ipend; /* Device interrupt is pending */
    uint spurious;  /* Count of spurious interrupts */
    uint pollrate;  /* Ticks between device polls */
    uint polltick;  /* Tick counter */

#ifdef DEBUG
    uint console_interval;
    struct brcmf_console console; /* Console output polling support */
    uint console_addr;            /* Console address from shared struct */
#endif                            /* DEBUG */

    uint clkstate;    /* State of sd and backplane clock(s) */
    int32_t idletime;     /* Control for activity timeout */
    int32_t idlecount;    /* Activity timeout counter */
    int32_t idleclock;    /* How to set bus driver when idle */
    bool rxflow_mode; /* Rx flow control mode */
    bool rxflow;      /* Is rx flow control on */
    bool alp_only;    /* Don't use HT clock (ALP only) */

    uint8_t* ctrl_frame_buf;
    uint16_t ctrl_frame_len;
    bool ctrl_frame_stat;
    zx_status_t ctrl_frame_err;

    // spinlock_t txq_lock; /* protect bus->txq */
    sync_completion_t ctrl_wait;
    sync_completion_t dcmd_resp_wait;

    brcmf_timer_info_t timer;
    sync_completion_t watchdog_wait;
    struct task_struct* watchdog_tsk;
    bool wd_active;

    struct workqueue_struct* brcmf_wq;
    struct work_struct datawork;
    bool dpc_triggered;
    bool dpc_running;

    bool txoff; /* Transmit flow-controlled */
    struct brcmf_sdio_count sdcnt;
    bool sr_enabled; /* SaveRestore enabled */
    bool sleeping;

    uint8_t tx_hdrlen;      /* sdio bus header length for tx packet */
    bool txglom;       /* host tx glomming enable flag */
    uint16_t head_align;    /* buffer pointer alignment */
    uint16_t sgentry_align; /* scatter-gather buffer alignment */
};

/* clkstate */
#define CLK_NONE 0
#define CLK_SDONLY 1
#define CLK_PENDING 2
#define CLK_AVAIL 3

#ifdef DEBUG
static int qcount[NUMPRIO];
#endif /* DEBUG */

#define DEFAULT_SDIO_DRIVE_STRENGTH 6 /* in milliamps */

#define RETRYCHAN(chan) ((chan) == SDPCM_EVENT_CHANNEL)

/* Limit on rounding up frames */
static const uint max_roundup = 512;

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
#define ALIGNMENT 8
#else
#define ALIGNMENT 4
#endif

enum brcmf_sdio_frmtype {
    BRCMF_SDIO_FT_NORMAL,
    BRCMF_SDIO_FT_SUPER,
    BRCMF_SDIO_FT_SUB,
};

#define SDIOD_DRVSTR_KEY(chip, pmu) (((chip) << 16) | (pmu))

/* SDIO Pad drive strength to select value mappings */
struct sdiod_drive_str {
    uint8_t strength; /* Pad Drive Strength in mA */
    uint8_t sel;      /* Chip-specific select value */
};

/* SDIO Drive Strength to sel value table for PMU Rev 11 (1.8V) */
static const struct sdiod_drive_str sdiod_drvstr_tab1_1v8[] = {
    {32, 0x6}, {26, 0x7}, {22, 0x4}, {16, 0x5}, {12, 0x2}, {8, 0x3}, {4, 0x0}, {0, 0x1}
};

/* SDIO Drive Strength to sel value table for PMU Rev 13 (1.8v) */
static const struct sdiod_drive_str sdiod_drive_strength_tab5_1v8[] = {
    {6, 0x7}, {5, 0x6}, {4, 0x5}, {3, 0x4}, {2, 0x2}, {1, 0x1}, {0, 0x0}
};

/* SDIO Drive Strength to sel value table for PMU Rev 17 (1.8v) */
static const struct sdiod_drive_str sdiod_drvstr_tab6_1v8[] = {
    {3, 0x3}, {2, 0x2}, {1, 0x1}, {0, 0x0}
};

/* SDIO Drive Strength to sel value table for 43143 PMU Rev 17 (3.3V) */
static const struct sdiod_drive_str sdiod_drvstr_tab2_3v3[] = {
    {16, 0x7}, {12, 0x5}, {8, 0x3}, {4, 0x1}
};

BRCMF_FW_NVRAM_DEF(43143, "brcmfmac43143-sdio.bin", "brcmfmac43143-sdio.txt");
BRCMF_FW_NVRAM_DEF(43241B0, "brcmfmac43241b0-sdio.bin", "brcmfmac43241b0-sdio.txt");
BRCMF_FW_NVRAM_DEF(43241B4, "brcmfmac43241b4-sdio.bin", "brcmfmac43241b4-sdio.txt");
BRCMF_FW_NVRAM_DEF(43241B5, "brcmfmac43241b5-sdio.bin", "brcmfmac43241b5-sdio.txt");
BRCMF_FW_NVRAM_DEF(4329, "brcmfmac4329-sdio.bin", "brcmfmac4329-sdio.txt");
BRCMF_FW_NVRAM_DEF(4330, "brcmfmac4330-sdio.bin", "brcmfmac4330-sdio.txt");
BRCMF_FW_NVRAM_DEF(4334, "brcmfmac4334-sdio.bin", "brcmfmac4334-sdio.txt");
BRCMF_FW_NVRAM_DEF(43340, "brcmfmac43340-sdio.bin", "brcmfmac43340-sdio.txt");
BRCMF_FW_NVRAM_DEF(4335, "brcmfmac4335-sdio.bin", "brcmfmac4335-sdio.txt");
BRCMF_FW_NVRAM_DEF(43362, "brcmfmac43362-sdio.bin", "brcmfmac43362-sdio.txt");
BRCMF_FW_NVRAM_DEF(4339, "brcmfmac4339-sdio.bin", "brcmfmac4339-sdio.txt");
BRCMF_FW_NVRAM_DEF(43430A0, "brcmfmac43430a0-sdio.bin", "brcmfmac43430a0-sdio.txt");
/* Note the names are not postfixed with a1 for backward compatibility */
BRCMF_FW_NVRAM_DEF(43430A1, "brcmfmac43430-sdio.bin", "brcmfmac43430-sdio.txt");
BRCMF_FW_NVRAM_DEF(43455, "brcmfmac43455-sdio.bin", "brcmfmac43455-sdio.txt");
BRCMF_FW_NVRAM_DEF(4354, "brcmfmac4354-sdio.bin", "brcmfmac4354-sdio.txt");
BRCMF_FW_NVRAM_DEF(4356, "brcmfmac4356-sdio.bin", "brcmfmac4356-sdio.txt");
BRCMF_FW_NVRAM_DEF(4373, "brcmfmac4373-sdio.bin", "brcmfmac4373-sdio.txt");

static struct brcmf_firmware_mapping brcmf_sdio_fwnames[] = {
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43143_CHIP_ID, 0xFFFFFFFF, 43143),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43241_CHIP_ID, 0x0000001F, 43241B0),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43241_CHIP_ID, 0x00000020, 43241B4),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43241_CHIP_ID, 0xFFFFFFC0, 43241B5),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4329_CHIP_ID, 0xFFFFFFFF, 4329),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4330_CHIP_ID, 0xFFFFFFFF, 4330),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4334_CHIP_ID, 0xFFFFFFFF, 4334),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43340_CHIP_ID, 0xFFFFFFFF, 43340),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43341_CHIP_ID, 0xFFFFFFFF, 43340),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4335_CHIP_ID, 0xFFFFFFFF, 4335),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43362_CHIP_ID, 0xFFFFFFFE, 43362),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4339_CHIP_ID, 0xFFFFFFFF, 4339),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43430_CHIP_ID, 0x00000001, 43430A0),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43430_CHIP_ID, 0xFFFFFFFE, 43430A1),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4345_CHIP_ID, 0xFFFFFFC0, 43455),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4354_CHIP_ID, 0xFFFFFFFF, 4354),
    BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4356_CHIP_ID, 0xFFFFFFFF, 4356),
    BRCMF_FW_NVRAM_ENTRY(CY_CC_4373_CHIP_ID, 0xFFFFFFFF, 4373)
};

static void pkt_align(struct brcmf_netbuf* p, int len, int align) {
    uint datalign;
    datalign = (unsigned long)(p->data);
    datalign = roundup(datalign, (align)) - datalign;
    if (datalign) {
        brcmf_netbuf_shrink_head(p, datalign);
    }
    brcmf_netbuf_set_length_to(p, len);
}

/* To check if there's window offered */
static bool data_ok(struct brcmf_sdio* bus) {
    return (uint8_t)(bus->tx_max - bus->tx_seq) != 0 &&
            ((uint8_t)(bus->tx_max - bus->tx_seq) & 0x80) == 0;
}

static zx_status_t brcmf_sdio_kso_control(struct brcmf_sdio* bus, bool on) {
    uint8_t wr_val = 0;
    uint8_t rd_val, cmp_val, bmask;
    zx_status_t err = ZX_OK;
    int err_cnt = 0;
    int try_cnt = 0;

    brcmf_dbg(TRACE, "Enter: on=%d\n", on);

    wr_val = (on << SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT);
    /* 1st KSO write goes to AOS wake up core if device is asleep  */
    brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, wr_val, &err);

    if (on) {
        /* device WAKEUP through KSO:
         * write bit 0 & read back until
         * both bits 0 (kso bit) & 1 (dev on status) are set
         */
        cmp_val = SBSDIO_FUNC1_SLEEPCSR_KSO_MASK | SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK;
        bmask = cmp_val;
        usleep_range(2000, 3000);
    } else {
        /* Put device to sleep, turn off KSO */
        cmp_val = 0;
        /* only check for bit0, bit1(dev on status) may not
         * get cleared right away
         */
        bmask = SBSDIO_FUNC1_SLEEPCSR_KSO_MASK;
    }

    do {
        /* reliable KSO bit set/clr:
         * the sdiod sleep write access is synced to PMU 32khz clk
         * just one write attempt may fail,
         * read it back until it matches written value
         */
        rd_val = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, &err);
        if (err == ZX_OK) {
            if ((rd_val & bmask) == cmp_val) {
                break;
            }
            err_cnt = 0;
        }
        /* bail out upon subsequent access errors */
        if (err != ZX_OK && (err_cnt++ > BRCMF_SDIO_MAX_ACCESS_ERRORS)) {
            break;
        }

        usleep(KSO_WAIT_US);
        brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, wr_val, &err);

    } while (try_cnt++ < MAX_KSO_ATTEMPTS);

    if (try_cnt > 2) {
        brcmf_dbg(SDIO, "try_cnt=%d rd_val=0x%x err=%d\n", try_cnt, rd_val, err);
    }

    if (try_cnt > MAX_KSO_ATTEMPTS) {
        brcmf_err("max tries: rd_val=0x%x err=%d\n", rd_val, err);
    }

    return err;
}

#define HOSTINTMASK (I_HMB_SW_MASK | I_CHIPACTIVE)

/* Turn backplane clock on or off */
static zx_status_t brcmf_sdio_htclk(struct brcmf_sdio* bus, bool on, bool pendok) {
    zx_status_t err;
    uint8_t clkctl, clkreq, devctl;
    zx_time_t timeout;

    brcmf_dbg(SDIO, "Enter\n");

    clkctl = 0;

    if (bus->sr_enabled) {
        bus->clkstate = (on ? CLK_AVAIL : CLK_SDONLY);
        return ZX_OK;
    }

    if (on) {
        /* Request HT Avail */
        clkreq = bus->alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;

        brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
        if (err != ZX_OK) {
            brcmf_err("HT Avail request error: %d\n", err);
            return ZX_ERR_IO_REFUSED;
        }

        /* Check current status */
        clkctl = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
        if (err != ZX_OK) {
            brcmf_err("HT Avail read error: %d\n", err);
            return ZX_ERR_IO_REFUSED;
        }

        /* Go to pending and await interrupt if appropriate */
        if (!SBSDIO_CLKAV(clkctl, bus->alp_only) && pendok) {
            /* Allow only clock-available interrupt */
            devctl = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
            if (err != ZX_OK) {
                brcmf_err("Devctl error setting CA: %d\n", err);
                return ZX_ERR_IO_REFUSED;
            }

            devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
            brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_DEVICE_CTL, devctl, &err);
            brcmf_dbg(SDIO, "CLKCTL: set PENDING\n");
            bus->clkstate = CLK_PENDING;

            return ZX_OK;
        } else if (bus->clkstate == CLK_PENDING) {
            /* Cancel CA-only interrupt filter */
            devctl = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
            devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
            brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_DEVICE_CTL, devctl, &err);
        }

        /* Otherwise, wait here (polling) for HT Avail */
        timeout = zx_clock_get(ZX_CLOCK_MONOTONIC) + ZX_USEC(PMU_MAX_TRANSITION_DLY_USEC);
        while (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
            clkctl = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
            if (zx_clock_get(ZX_CLOCK_MONOTONIC) > timeout) {
                break;
            } else {
                usleep_range(5000, 10000);
            }
        }
        if (err != ZX_OK) {
            brcmf_err("HT Avail request error: %d\n", err);
            return ZX_ERR_IO_REFUSED;
        }
        if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
            brcmf_err("HT Avail timeout (%d): clkctl 0x%02x\n", PMU_MAX_TRANSITION_DLY_USEC,
                      clkctl);
            return ZX_ERR_SHOULD_WAIT;
        }

        /* Mark clock available */
        bus->clkstate = CLK_AVAIL;
        brcmf_dbg(SDIO, "CLKCTL: turned ON\n");

#if defined(DEBUG)
        if (!bus->alp_only) {
            if (SBSDIO_ALPONLY(clkctl)) {
                brcmf_err("HT Clock should be on\n");
            }
        }
#endif /* defined (DEBUG) */

    } else {
        clkreq = 0;

        if (bus->clkstate == CLK_PENDING) {
            /* Cancel CA-only interrupt filter */
            devctl = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
            devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
            brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_DEVICE_CTL, devctl, &err);
        }

        bus->clkstate = CLK_SDONLY;
        brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
        brcmf_dbg(SDIO, "CLKCTL: turned OFF\n");
        if (err != ZX_OK) {
            brcmf_err("Failed access turning clock off: %d\n", err);
            return ZX_ERR_IO_REFUSED;
        }
    }
    return ZX_OK;
}

/* Change idle/active SD state */
static zx_status_t brcmf_sdio_sdclk(struct brcmf_sdio* bus, bool on) {
    brcmf_dbg(SDIO, "Enter\n");

    if (on) {
        bus->clkstate = CLK_SDONLY;
    } else {
        bus->clkstate = CLK_NONE;
    }

    return ZX_OK;
}

/* Transition SD and backplane clock readiness */
static zx_status_t brcmf_sdio_clkctl(struct brcmf_sdio* bus, uint target, bool pendok) {
#ifdef DEBUG
    uint oldstate = bus->clkstate;
#endif /* DEBUG */

    brcmf_dbg(SDIO, "Enter\n");

    /* Early exit if we're already there */
    if (bus->clkstate == target) {
        return ZX_OK;
    }

    switch (target) {
    case CLK_AVAIL:
        /* Make sure SD clock is available */
        if (bus->clkstate == CLK_NONE) {
            brcmf_sdio_sdclk(bus, true);
        }
        /* Now request HT Avail on the backplane */
        brcmf_sdio_htclk(bus, true, pendok);
        break;

    case CLK_SDONLY:
        /* Remove HT request, or bring up SD clock */
        if (bus->clkstate == CLK_NONE) {
            brcmf_sdio_sdclk(bus, true);
        } else if (bus->clkstate == CLK_AVAIL) {
            brcmf_sdio_htclk(bus, false, false);
        } else {
            brcmf_err("request for %d -> %d\n", bus->clkstate, target);
        }
        break;

    case CLK_NONE:
        /* Make sure to remove HT request */
        if (bus->clkstate == CLK_AVAIL) {
            brcmf_sdio_htclk(bus, false, false);
        }
        /* Now remove the SD clock */
        brcmf_sdio_sdclk(bus, false);
        break;
    }
#ifdef DEBUG
    brcmf_dbg(SDIO, "%d -> %d\n", oldstate, bus->clkstate);
#endif /* DEBUG */

    return ZX_OK;
}

static zx_status_t brcmf_sdio_bus_sleep(struct brcmf_sdio* bus, bool sleep, bool pendok) {
    zx_status_t err = ZX_OK;
    uint8_t clkcsr;

    brcmf_dbg(SDIO, "Enter: request %s currently %s\n", (sleep ? "SLEEP" : "WAKE"),
              (bus->sleeping ? "SLEEP" : "WAKE"));

    /* If SR is enabled control bus state with KSO */
    if (bus->sr_enabled) {
        /* Done if we're already in the requested state */
        if (sleep == bus->sleeping) {
            goto end;
        }

        /* Going to sleep */
        if (sleep) {
            clkcsr = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
            if ((clkcsr & SBSDIO_CSR_MASK) == 0) {
                brcmf_dbg(SDIO, "no clock, set ALP\n");
                brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_ALP_AVAIL_REQ,
                                   &err);
            }
            err = brcmf_sdio_kso_control(bus, false);
        } else {
            err = brcmf_sdio_kso_control(bus, true);
        }
        if (err != ZX_OK) {
            brcmf_err("error while changing bus sleep state %d\n", err);
            goto done;
        }
    }

end:
    /* control clocks */
    if (sleep) {
        if (!bus->sr_enabled) {
            brcmf_sdio_clkctl(bus, CLK_NONE, pendok);
        }
    } else {
        brcmf_sdio_clkctl(bus, CLK_AVAIL, pendok);
        brcmf_sdio_wd_timer(bus, true);
    }
    bus->sleeping = sleep;
    brcmf_dbg(SDIO, "new state %s\n", (sleep ? "SLEEP" : "WAKE"));
done:
    brcmf_dbg(SDIO, "Exit: err=%d\n", err);
    return err;
}

#ifdef DEBUG
static inline bool brcmf_sdio_valid_shared_address(uint32_t addr) {
    return !(addr == 0 || ((~addr >> 16) & 0xffff) == (addr & 0xffff));
}

static zx_status_t brcmf_sdio_readshared(struct brcmf_sdio* bus, struct sdpcm_shared* sh) {
    uint32_t addr = 0;
    zx_status_t rv;
    uint32_t shaddr = 0;
    struct sdpcm_shared_le sh_le;
    uint32_t addr_le;

    sdio_claim_host(bus->sdiodev->func1);
    brcmf_sdio_bus_sleep(bus, false, false);

    /*
     * Read last word in socram to determine
     * address of sdpcm_shared structure
     */
    shaddr = bus->ci->rambase + bus->ci->ramsize - 4;
    if (!bus->ci->rambase && brcmf_chip_sr_capable(bus->ci)) {
        shaddr -= bus->ci->srsize;
    }
    rv = brcmf_sdiod_ramrw(bus->sdiodev, false, shaddr, (uint8_t*)&addr_le, 4);
    if (rv != ZX_OK) {
        goto fail;
    }

    /*
     * Check if addr is valid.
     * NVRAM length at the end of memory should have been overwritten.
     */
    addr = addr_le;
    if (!brcmf_sdio_valid_shared_address(addr)) {
        brcmf_err("invalid sdpcm_shared address 0x%08X\n", addr);
        rv = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    brcmf_dbg(INFO, "sdpcm_shared address 0x%08X\n", addr);

    /* Read hndrte_shared structure */
    rv = brcmf_sdiod_ramrw(bus->sdiodev, false, addr, (uint8_t*)&sh_le,
                           sizeof(struct sdpcm_shared_le));
    if (rv != ZX_OK) {
        goto fail;
    }

    sdio_release_host(bus->sdiodev->func1);

    /* Endianness */
    sh->flags = sh_le.flags;
    sh->trap_addr = sh_le.trap_addr;
    sh->assert_exp_addr = sh_le.assert_exp_addr;
    sh->assert_file_addr = sh_le.assert_file_addr;
    sh->assert_line = sh_le.assert_line;
    sh->console_addr = sh_le.console_addr;
    sh->msgtrace_addr = sh_le.msgtrace_addr;

    if ((sh->flags & SDPCM_SHARED_VERSION_MASK) > SDPCM_SHARED_VERSION) {
        brcmf_err("sdpcm shared version unsupported: dhd %d dongle %d\n", SDPCM_SHARED_VERSION,
                  sh->flags & SDPCM_SHARED_VERSION_MASK);
        return ZX_ERR_WRONG_TYPE;
    }
    return ZX_OK;

fail:
    brcmf_err("unable to obtain sdpcm_shared info: rv=%d (addr=0x%x)\n", rv, addr);
    sdio_release_host(bus->sdiodev->func1);
    return rv;
}

static void brcmf_sdio_get_console_addr(struct brcmf_sdio* bus) {
    struct sdpcm_shared sh;

    if (brcmf_sdio_readshared(bus, &sh) == ZX_OK) {
        bus->console_addr = sh.console_addr;
    }
}
#else
static void brcmf_sdio_get_console_addr(struct brcmf_sdio* bus) {}
#endif /* DEBUG */

static uint32_t brcmf_sdio_hostmail(struct brcmf_sdio* bus) {
    struct brcmf_sdio_dev* sdiod = bus->sdiodev;
    struct brcmf_core* core = bus->sdio_core;
    uint32_t intstatus = 0;
    uint32_t hmb_data;
    uint8_t fcbits;
    zx_status_t ret;

    brcmf_dbg(SDIO, "Enter\n");

    /* Read mailbox data and ack that we did so */
    hmb_data = brcmf_sdiod_readl(sdiod, core->base + SD_REG(tohostmailboxdata), &ret);

    if (ret == ZX_OK) {
        brcmf_sdiod_writel(sdiod, core->base + SD_REG(tosbmailbox), SMB_INT_ACK, &ret);
    }

    bus->sdcnt.f1regdata += 2;

    /* dongle indicates the firmware has halted/crashed */
    if (hmb_data & HMB_DATA_FWHALT) {
        brcmf_err("mailbox indicates firmware halted\n");
    }

    /* Dongle recomposed rx frames, accept them again */
    if (hmb_data & HMB_DATA_NAKHANDLED) {
        brcmf_dbg(SDIO, "Dongle reports NAK handled, expect rtx of %d\n", bus->rx_seq);
        if (!bus->rxskip) {
            brcmf_err("unexpected NAKHANDLED!\n");
        }

        bus->rxskip = false;
        intstatus |= I_HMB_FRAME_IND;
    }

    /*
     * DEVREADY does not occur with gSPI.
     */
    if (hmb_data & (HMB_DATA_DEVREADY | HMB_DATA_FWREADY)) {
        bus->sdpcm_ver = (hmb_data & HMB_DATA_VERSION_MASK) >> HMB_DATA_VERSION_SHIFT;
        if (bus->sdpcm_ver != SDPCM_PROT_VERSION) {
            brcmf_err(
                "Version mismatch, dongle reports %d, "
                "expecting %d\n",
                bus->sdpcm_ver, SDPCM_PROT_VERSION);
        } else {
            brcmf_dbg(SDIO, "Dongle ready, protocol version %d\n", bus->sdpcm_ver);
        }

        /*
         * Retrieve console state address now that firmware should have
         * updated it.
         */
        brcmf_sdio_get_console_addr(bus);
    }

    /*
     * Flow Control has been moved into the RX headers and this out of band
     * method isn't used any more.
     * remaining backward compatible with older dongles.
     */
    if (hmb_data & HMB_DATA_FC) {
        fcbits = (hmb_data & HMB_DATA_FCDATA_MASK) >> HMB_DATA_FCDATA_SHIFT;

        if (fcbits & ~bus->flowcontrol) {
            bus->sdcnt.fc_xoff++;
        }

        if (bus->flowcontrol & ~fcbits) {
            bus->sdcnt.fc_xon++;
        }

        bus->sdcnt.fc_rcvd++;
        bus->flowcontrol = fcbits;
    }

    /* Shouldn't be any others */
    if (hmb_data & ~(HMB_DATA_DEVREADY | HMB_DATA_NAKHANDLED | HMB_DATA_FC | HMB_DATA_FWREADY |
                     HMB_DATA_FWHALT | HMB_DATA_FCDATA_MASK | HMB_DATA_VERSION_MASK)) {
        brcmf_err("Unknown mailbox data content: 0x%02x\n", hmb_data);
    }

    return intstatus;
}

static void brcmf_sdio_rxfail(struct brcmf_sdio* bus, bool abort, bool rtx) {
    struct brcmf_sdio_dev* sdiod = bus->sdiodev;
    struct brcmf_core* core = bus->sdio_core;
    uint retries = 0;
    uint16_t lastrbc;
    uint8_t hi, lo;
    zx_status_t err;

    brcmf_err("%sterminate frame%s\n", abort ? "abort command, " : "", rtx ? ", send NAK" : "");

    if (abort) {
        brcmf_sdiod_abort(bus->sdiodev, SDIO_FN_2);
    }

    brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_FRAMECTRL, SFC_RF_TERM, &err);
    bus->sdcnt.f1regdata++;

    /* Wait until the packet has been flushed (device/FIFO stable) */
    for (lastrbc = retries = 0xffff; retries > 0; retries--) {
        hi = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_RFRAMEBCHI, &err);
        lo = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_RFRAMEBCLO, &err);
        bus->sdcnt.f1regdata += 2;

        if ((hi == 0) && (lo == 0)) {
            break;
        }

        if ((hi > (lastrbc >> 8)) && (lo > (lastrbc & 0x00ff))) {
            brcmf_err("count growing: last 0x%04x now 0x%04x\n", lastrbc, (hi << 8) + lo);
        }
        lastrbc = (hi << 8) + lo;
    }

    if (!retries) {
        brcmf_err("count never zeroed: last 0x%04x\n", lastrbc);
    } else {
        brcmf_dbg(SDIO, "flush took %d iterations\n", 0xffff - retries);
    }

    if (rtx) {
        bus->sdcnt.rxrtx++;
        brcmf_sdiod_writel(sdiod, core->base + SD_REG(tosbmailbox), SMB_NAK, &err);

        bus->sdcnt.f1regdata++;
        if (err == ZX_OK) {
            bus->rxskip = true;
        }
    }

    /* Clear partial in any case */
    bus->cur_read.len = 0;
}

static void brcmf_sdio_txfail(struct brcmf_sdio* bus) {
    struct brcmf_sdio_dev* sdiodev = bus->sdiodev;
    uint8_t i, hi, lo;

    /* On failure, abort the command and terminate the frame */
    brcmf_err("sdio error, abort command and terminate frame\n");
    bus->sdcnt.tx_sderrs++;

    brcmf_sdiod_abort(sdiodev, SDIO_FN_2);
    brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_FRAMECTRL, SFC_WF_TERM, NULL);
    bus->sdcnt.f1regdata++;

    for (i = 0; i < 3; i++) {
        hi = brcmf_sdiod_readb(sdiodev, SBSDIO_FUNC1_WFRAMEBCHI, NULL);
        lo = brcmf_sdiod_readb(sdiodev, SBSDIO_FUNC1_WFRAMEBCLO, NULL);
        bus->sdcnt.f1regdata += 2;
        if ((hi == 0) && (lo == 0)) {
            break;
        }
    }
}

/* return total length of buffer chain */
static uint brcmf_sdio_glom_len(struct brcmf_sdio* bus) {
    struct brcmf_netbuf* p;
    uint total;

    total = 0;
    brcmf_netbuf_list_for_every(&bus->glom, p) total += p->len;
    return total;
}

static void brcmf_sdio_free_glom(struct brcmf_sdio* bus) {
    struct brcmf_netbuf* cur;
    struct brcmf_netbuf* next;

    brcmf_netbuf_list_for_every_safe(&bus->glom, cur, next) {
        brcmf_netbuf_list_remove(&bus->glom, cur);
        brcmu_pkt_buf_free_netbuf(cur);
    }
}

/**
 * brcmfmac sdio bus specific header
 * This is the lowest layer header wrapped on the packets transmitted between
 * host and WiFi dongle which contains information needed for SDIO core and
 * firmware
 *
 * It consists of 3 parts: hardware header, hardware extension header and
 * software header
 * hardware header (frame tag) - 4 bytes
 * Byte 0~1: Frame length
 * Byte 2~3: Checksum, bit-wise inverse of frame length
 * hardware extension header - 8 bytes
 * Tx glom mode only, N/A for Rx or normal Tx
 * Byte 0~1: Packet length excluding hw frame tag
 * Byte 2: Reserved
 * Byte 3: Frame flags, bit 0: last frame indication
 * Byte 4~5: Reserved
 * Byte 6~7: Tail padding length
 * software header - 8 bytes
 * Byte 0: Rx/Tx sequence number
 * Byte 1: 4 MSB Channel number, 4 LSB arbitrary flag
 * Byte 2: Length of next data frame, reserved for Tx
 * Byte 3: Data offset
 * Byte 4: Flow control bits, reserved for Tx
 * Byte 5: Maximum Sequence number allowed by firmware for Tx, N/A for Tx packet
 * Byte 6~7: Reserved
 */
#define SDPCM_HWHDR_LEN 4
#define SDPCM_HWEXT_LEN 8
#define SDPCM_SWHDR_LEN 8
#define SDPCM_HDRLEN (SDPCM_HWHDR_LEN + SDPCM_SWHDR_LEN)
/* software header */
#define SDPCM_SEQ_MASK 0x000000ff
#define SDPCM_SEQ_WRAP 256
#define SDPCM_CHANNEL_MASK 0x00000f00
#define SDPCM_CHANNEL_SHIFT 8
#define SDPCM_CONTROL_CHANNEL 0 /* Control */
#define SDPCM_EVENT_CHANNEL 1   /* Asyc Event Indication */
#define SDPCM_DATA_CHANNEL 2    /* Data Xmit/Recv */
#define SDPCM_GLOM_CHANNEL 3    /* Coalesced packets */
#define SDPCM_TEST_CHANNEL 15   /* Test/debug packets */
#define SDPCM_GLOMDESC(p) (((uint8_t*)p)[1] & 0x80)
#define SDPCM_NEXTLEN_MASK 0x00ff0000
#define SDPCM_NEXTLEN_SHIFT 16
#define SDPCM_DOFFSET_MASK 0xff000000
#define SDPCM_DOFFSET_SHIFT 24
#define SDPCM_FCMASK_MASK 0x000000ff
#define SDPCM_WINDOW_MASK 0x0000ff00
#define SDPCM_WINDOW_SHIFT 8

static inline uint8_t brcmf_sdio_getdatoffset(uint8_t* swheader) {
    uint32_t hdrvalue;
    hdrvalue = *(uint32_t*)swheader;
    return (uint8_t)((hdrvalue & SDPCM_DOFFSET_MASK) >> SDPCM_DOFFSET_SHIFT);
}

static inline bool brcmf_sdio_fromevntchan(uint8_t* swheader) {
    uint32_t hdrvalue;
    uint8_t ret;

    hdrvalue = *(uint32_t*)swheader;
    ret = (uint8_t)((hdrvalue & SDPCM_CHANNEL_MASK) >> SDPCM_CHANNEL_SHIFT);

    return (ret == SDPCM_EVENT_CHANNEL);
}

static zx_status_t brcmf_sdio_hdparse(struct brcmf_sdio* bus, uint8_t* header,
                                      struct brcmf_sdio_hdrinfo* rd, enum brcmf_sdio_frmtype type) {
    uint16_t len, checksum;
    uint8_t rx_seq, fc, tx_seq_max;
    uint32_t swheader;

    //trace_brcmf_sdpcm_hdr(SDPCM_RX, header);

    /* hw header */
    len = get_unaligned_le16(header);
    checksum = get_unaligned_le16(header + sizeof(uint16_t));
    /* All zero means no more to read */
    if (!(len | checksum)) {
        bus->rxpending = false;
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    if ((uint16_t)(~(len ^ checksum))) {
        brcmf_err("HW header checksum error\n");
        bus->sdcnt.rx_badhdr++;
        brcmf_sdio_rxfail(bus, false, false);
        return ZX_ERR_IO;
    }
    if (len < SDPCM_HDRLEN) {
        brcmf_err("HW header length error\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (type == BRCMF_SDIO_FT_SUPER && (roundup(len, bus->blocksize) != rd->len)) {
        brcmf_err("HW superframe header length error\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (type == BRCMF_SDIO_FT_SUB && len > rd->len) {
        brcmf_err("HW subframe header length error\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    rd->len = len;

    /* software header */
    header += SDPCM_HWHDR_LEN;
    swheader = *(uint32_t*)header;
    if (type == BRCMF_SDIO_FT_SUPER && SDPCM_GLOMDESC(header)) {
        brcmf_err("Glom descriptor found in superframe head\n");
        rd->len = 0;
        return ZX_ERR_INVALID_ARGS;
    }
    rx_seq = (uint8_t)(swheader & SDPCM_SEQ_MASK);
    rd->channel = (swheader & SDPCM_CHANNEL_MASK) >> SDPCM_CHANNEL_SHIFT;
    if (len > MAX_RX_DATASZ && rd->channel != SDPCM_CONTROL_CHANNEL &&
            type != BRCMF_SDIO_FT_SUPER) {
        brcmf_err("HW header length too long\n");
        bus->sdcnt.rx_toolong++;
        brcmf_sdio_rxfail(bus, false, false);
        rd->len = 0;
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (type == BRCMF_SDIO_FT_SUPER && rd->channel != SDPCM_GLOM_CHANNEL) {
        brcmf_err("Wrong channel for superframe\n");
        rd->len = 0;
        return ZX_ERR_INVALID_ARGS;
    }
    if (type == BRCMF_SDIO_FT_SUB && rd->channel != SDPCM_DATA_CHANNEL &&
            rd->channel != SDPCM_EVENT_CHANNEL) {
        brcmf_err("Wrong channel for subframe\n");
        rd->len = 0;
        return ZX_ERR_INVALID_ARGS;
    }
    rd->dat_offset = brcmf_sdio_getdatoffset(header);
    if (rd->dat_offset < SDPCM_HDRLEN || rd->dat_offset > rd->len) {
        brcmf_err("seq %d: bad data offset\n", rx_seq);
        bus->sdcnt.rx_badhdr++;
        brcmf_sdio_rxfail(bus, false, false);
        rd->len = 0;
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (rd->seq_num != rx_seq) {
        brcmf_dbg(SDIO, "seq %d, expected %d\n", rx_seq, rd->seq_num);
        bus->sdcnt.rx_badseq++;
        rd->seq_num = rx_seq;
    }
    /* no need to check the reset for subframe */
    if (type == BRCMF_SDIO_FT_SUB) {
        return ZX_OK;
    }
    rd->len_nxtfrm = (swheader & SDPCM_NEXTLEN_MASK) >> SDPCM_NEXTLEN_SHIFT;
    if (rd->len_nxtfrm << 4 > MAX_RX_DATASZ) {
        /* only warm for NON glom packet */
        if (rd->channel != SDPCM_GLOM_CHANNEL) {
            brcmf_err("seq %d: next length error\n", rx_seq);
        }
        rd->len_nxtfrm = 0;
    }
    swheader = *(uint32_t*)(header + 4);
    fc = swheader & SDPCM_FCMASK_MASK;
    if (bus->flowcontrol != fc) {
        if (~bus->flowcontrol & fc) {
            bus->sdcnt.fc_xoff++;
        }
        if (bus->flowcontrol & ~fc) {
            bus->sdcnt.fc_xon++;
        }
        bus->sdcnt.fc_rcvd++;
        bus->flowcontrol = fc;
    }
    tx_seq_max = (swheader & SDPCM_WINDOW_MASK) >> SDPCM_WINDOW_SHIFT;
    if ((uint8_t)(tx_seq_max - bus->tx_seq) > 0x40) {
        brcmf_err("seq %d: max tx seq number error\n", rx_seq);
        tx_seq_max = bus->tx_seq + 2;
    }
    bus->tx_max = tx_seq_max;

    return ZX_OK;
}

static inline void brcmf_sdio_update_hwhdr(uint8_t* header, uint16_t frm_length) {
    *(uint16_t*)header = frm_length;
    *(((uint16_t*)header) + 1) = ~frm_length;
}

static void brcmf_sdio_hdpack(struct brcmf_sdio* bus, uint8_t* header,
                              struct brcmf_sdio_hdrinfo* hd_info) {
    uint32_t hdrval;
    uint8_t hdr_offset;

    brcmf_sdio_update_hwhdr(header, hd_info->len);
    hdr_offset = SDPCM_HWHDR_LEN;

    if (bus->txglom) {
        hdrval = (hd_info->len - hdr_offset) | (hd_info->lastfrm << 24);
        *((uint32_t*)(header + hdr_offset)) = hdrval;
        hdrval = (uint16_t)hd_info->tail_pad << 16;
        *(((uint32_t*)(header + hdr_offset)) + 1) = hdrval;
        hdr_offset += SDPCM_HWEXT_LEN;
    }

    hdrval = hd_info->seq_num;
    hdrval |= (hd_info->channel << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK;
    hdrval |= (hd_info->dat_offset << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK;
    *((uint32_t*)(header + hdr_offset)) = hdrval;
    *(((uint32_t*)(header + hdr_offset)) + 1) = 0;
    //trace_brcmf_sdpcm_hdr(SDPCM_TX + !!(bus->txglom), header);
}

static uint8_t brcmf_sdio_rxglom(struct brcmf_sdio* bus, uint8_t rxseq) {
    uint16_t dlen, totlen;
    uint8_t* dptr;
    uint8_t num = 0;
    uint16_t sublen;
    struct brcmf_netbuf* pfirst;
    struct brcmf_netbuf* pnext;

    zx_status_t errcode;
    uint8_t doff, sfdoff;

    struct brcmf_sdio_hdrinfo rd_new;

    /* If packets, issue read(s) and send up packet chain */
    /* Return sequence numbers consumed? */

    brcmf_dbg(SDIO, "start: glomd %p glom %p\n", bus->glomd,
              brcmf_netbuf_list_peek_head(&bus->glom));

    /* If there's a descriptor, generate the packet chain */
    if (bus->glomd) {
        pfirst = pnext = NULL;
        dlen = (uint16_t)(bus->glomd->len);
        dptr = bus->glomd->data;
        if (!dlen || (dlen & 1)) {
            brcmf_err("bad glomd len(%d), ignore descriptor\n", dlen);
            dlen = 0;
        }

        for (totlen = num = 0; dlen; num++) {
            /* Get (and move past) next length */
            sublen = get_unaligned_le16(dptr);
            dlen -= sizeof(uint16_t);
            dptr += sizeof(uint16_t);
            if ((sublen < SDPCM_HDRLEN) || ((num == 0) && (sublen < (2 * SDPCM_HDRLEN)))) {
                brcmf_err("descriptor len %d bad: %d\n", num, sublen);
                pnext = NULL;
                break;
            }
            if (sublen % bus->sgentry_align) {
                brcmf_err("sublen %d not multiple of %d\n", sublen, bus->sgentry_align);
            }
            totlen += sublen;

            /* For last frame, adjust read len so total
                     is a block multiple */
            if (!dlen) {
                sublen += (roundup(totlen, bus->blocksize) - totlen);
                totlen = roundup(totlen, bus->blocksize);
            }

            /* Allocate/chain packet for next subframe */
            pnext = brcmu_pkt_buf_get_netbuf(sublen + bus->sgentry_align);
            if (pnext == NULL) {
                brcmf_err("bcm_pkt_buf_get_netbuf failed, num %d len %d\n", num, sublen);
                break;
            }
            brcmf_netbuf_list_add_tail(&bus->glom, pnext);

            /* Adhere to start alignment requirements */
            pkt_align(pnext, sublen, bus->sgentry_align);
        }

        /* If all allocations succeeded, save packet chain
                 in bus structure */
        if (pnext) {
            brcmf_dbg(GLOM, "allocated %d-byte packet chain for %d subframes\n", totlen, num);
            if (BRCMF_GLOM_ON() && bus->cur_read.len && totlen != bus->cur_read.len) {
                brcmf_dbg(GLOM, "glomdesc mismatch: nextlen %d glomdesc %d rxseq %d\n",
                          bus->cur_read.len, totlen, rxseq);
            }
            pfirst = pnext = NULL;
        } else {
            brcmf_sdio_free_glom(bus);
            num = 0;
        }

        /* Done with descriptor packet */
        brcmu_pkt_buf_free_netbuf(bus->glomd);
        bus->glomd = NULL;
        bus->cur_read.len = 0;
    }

    /* Ok -- either we just generated a packet chain,
             or had one from before */
    if (!brcmf_netbuf_list_is_empty(&bus->glom)) {
        if (BRCMF_GLOM_ON()) {
            brcmf_dbg(GLOM, "try superframe read, packet chain:\n");
            brcmf_netbuf_list_for_every(&bus->glom, pnext) {
                brcmf_dbg(GLOM, "    %p: %p len 0x%04x (%d)\n", pnext, (uint8_t*)(pnext->data),
                          pnext->len, pnext->len);
            }
        }

        pfirst = brcmf_netbuf_list_peek_head(&bus->glom);
        dlen = (uint16_t)brcmf_sdio_glom_len(bus);

        /* Do an SDIO read for the superframe.  Configurable iovar to
         * read directly into the chained packet, or allocate a large
         * packet and and copy into the chain.
         */
        sdio_claim_host(bus->sdiodev->func1);
        errcode = brcmf_sdiod_recv_chain(bus->sdiodev, &bus->glom, dlen);
        sdio_release_host(bus->sdiodev->func1);
        bus->sdcnt.f2rxdata++;

        /* On failure, kill the superframe */
        if (errcode != ZX_OK) {
            brcmf_err("glom read of %d bytes failed: %d\n", dlen, errcode);

            sdio_claim_host(bus->sdiodev->func1);
            brcmf_sdio_rxfail(bus, true, false);
            bus->sdcnt.rxglomfail++;
            brcmf_sdio_free_glom(bus);
            sdio_release_host(bus->sdiodev->func1);
            return 0;
        }

        brcmf_dbg_hex_dump(BRCMF_GLOM_ON(), pfirst->data, min_t(int, pfirst->len, 48),
                           "SUPERFRAME:\n");

        rd_new.seq_num = rxseq;
        rd_new.len = dlen;
        sdio_claim_host(bus->sdiodev->func1);
        errcode = brcmf_sdio_hdparse(bus, pfirst->data, &rd_new, BRCMF_SDIO_FT_SUPER);
        sdio_release_host(bus->sdiodev->func1);
        bus->cur_read.len = rd_new.len_nxtfrm << 4;

        /* Remove superframe header, remember offset */
        brcmf_netbuf_shrink_head(pfirst, rd_new.dat_offset);
        sfdoff = rd_new.dat_offset;
        num = 0;

        /* Validate all the subframe headers */
        brcmf_netbuf_list_for_every(&bus->glom, pnext) {
            /* leave when invalid subframe is found */
            if (errcode != ZX_OK) {
                break;
            }

            rd_new.len = pnext->len;
            rd_new.seq_num = rxseq++;
            sdio_claim_host(bus->sdiodev->func1);
            errcode = brcmf_sdio_hdparse(bus, pnext->data, &rd_new, BRCMF_SDIO_FT_SUB);
            sdio_release_host(bus->sdiodev->func1);
            brcmf_dbg_hex_dump(BRCMF_GLOM_ON(), pnext->data, 32, "subframe:\n");

            num++;
        }

        if (errcode != ZX_OK) {
            /* Terminate frame on error */
            sdio_claim_host(bus->sdiodev->func1);
            brcmf_sdio_rxfail(bus, true, false);
            bus->sdcnt.rxglomfail++;
            brcmf_sdio_free_glom(bus);
            sdio_release_host(bus->sdiodev->func1);
            bus->cur_read.len = 0;
            return 0;
        }

        /* Basic SD framing looks ok - process each packet (header) */

        brcmf_netbuf_list_for_every_safe(&bus->glom, pfirst, pnext) {
            dptr = (uint8_t*)(pfirst->data);
            sublen = get_unaligned_le16(dptr);
            doff = brcmf_sdio_getdatoffset(&dptr[SDPCM_HWHDR_LEN]);

            brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_DATA_ON(), dptr, pfirst->len,
                               "Rx Subframe Data:\n");

            brcmf_netbuf_set_length_to(pfirst, sublen);
            brcmf_netbuf_shrink_head(pfirst, doff);

            if (pfirst->len == 0) {
                brcmf_netbuf_list_remove(&bus->glom, pfirst);
                brcmu_pkt_buf_free_netbuf(pfirst);
                continue;
            }

            brcmf_dbg_hex_dump(BRCMF_GLOM_ON(), pfirst->data, min_t(int, pfirst->len, 32),
                               "subframe %d to stack, %p (%p/%d) nxt/lnk %p/%p\n",
                               brcmf_netbuf_list_length(&bus->glom),
                               pfirst, pfirst->data, pfirst->len,
                               brcmf_netbuf_list_next(&bus->glom, pfirst),
                               brcmf_netbuf_list_prev(&bus->glom, pfirst));
            brcmf_netbuf_list_remove(&bus->glom, pfirst);
            if (brcmf_sdio_fromevntchan(&dptr[SDPCM_HWHDR_LEN])) {
                brcmf_rx_event(&bus->sdiodev->dev, pfirst);
            } else {
                brcmf_rx_frame(&bus->sdiodev->dev, pfirst, false);
            }
            bus->sdcnt.rxglompkts++;
        }

        bus->sdcnt.rxglomframes++;
    }
    return num;
}

static int brcmf_sdio_dcmd_resp_wait(struct brcmf_sdio* bus, bool* pending) {
    /* Wait until control frame is available */
    *pending = false; // TODO(cphoenix): Does signal_pending() have meaning in Garnet?
    return sync_completion_wait(&bus->dcmd_resp_wait, ZX_MSEC(DCMD_RESP_TIMEOUT_MSEC));
}

static zx_status_t brcmf_sdio_dcmd_resp_wake(struct brcmf_sdio* bus) {
    sync_completion_signal(&bus->dcmd_resp_wait);

    return ZX_OK;
}
static void brcmf_sdio_read_control(struct brcmf_sdio* bus, uint8_t* hdr, uint len, uint doff) {
    uint rdlen, pad;
    uint8_t* buf = NULL;
    uint8_t* rbuf;
    zx_status_t sdret;

    brcmf_dbg(TRACE, "Enter\n");

    if (bus->rxblen) {
        buf = calloc(1, bus->rxblen);
    }
    if (!buf) {
        goto done;
    }

    rbuf = bus->rxbuf;
    pad = ((unsigned long)rbuf % bus->head_align);
    if (pad) {
        rbuf += (bus->head_align - pad);
    }

    /* Copy the already-read portion over */
    memcpy(buf, hdr, BRCMF_FIRSTREAD);
    if (len <= BRCMF_FIRSTREAD) {
        goto gotpkt;
    }

    /* Raise rdlen to next SDIO block to avoid tail command */
    rdlen = len - BRCMF_FIRSTREAD;
    if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
        pad = bus->blocksize - (rdlen % bus->blocksize);
        if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
                ((len + pad) < bus->sdiodev->bus_if->maxctl)) {
            rdlen += pad;
        }
    } else if (rdlen % bus->head_align) {
        rdlen += bus->head_align - (rdlen % bus->head_align);
    }

    /* Drop if the read is too big or it exceeds our maximum */
    if ((rdlen + BRCMF_FIRSTREAD) > bus->sdiodev->bus_if->maxctl) {
        brcmf_err("%d-byte control read exceeds %d-byte buffer\n", rdlen,
                  bus->sdiodev->bus_if->maxctl);
        brcmf_sdio_rxfail(bus, false, false);
        goto done;
    }

    if ((len - doff) > bus->sdiodev->bus_if->maxctl) {
        brcmf_err("%d-byte ctl frame (%d-byte ctl data) exceeds %d-byte limit\n", len, len - doff,
                  bus->sdiodev->bus_if->maxctl);
        bus->sdcnt.rx_toolong++;
        brcmf_sdio_rxfail(bus, false, false);
        goto done;
    }

    /* Read remain of frame body */
    sdret = brcmf_sdiod_recv_buf(bus->sdiodev, rbuf, rdlen);
    bus->sdcnt.f2rxdata++;

    /* Control frame failures need retransmission */
    if (sdret != ZX_OK) {
        brcmf_err("read %d control bytes failed: %d\n", rdlen, sdret);
        bus->sdcnt.rxc_errors++;
        brcmf_sdio_rxfail(bus, true, true);
        goto done;
    } else {
        memcpy(buf + BRCMF_FIRSTREAD, rbuf, rdlen);
    }

gotpkt:

    brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_CTL_ON(), buf, len, "RxCtrl:\n");

    /* Point to valid data and indicate its length */
    //spin_lock_bh(&bus->rxctl_lock);
    pthread_mutex_lock(&irq_callback_lock);
    if (bus->rxctl) {
        brcmf_err("last control frame is being processed.\n");
        //spin_unlock_bh(&bus->rxctl_lock);
        pthread_mutex_unlock(&irq_callback_lock);
        free(buf);
        goto done;
    }
    bus->rxctl = buf + doff;
    bus->rxctl_orig = buf;
    bus->rxlen = len - doff;
    //spin_unlock_bh(&bus->rxctl_lock);
    pthread_mutex_unlock(&irq_callback_lock);

done:
    /* Awake any waiters */
    if (bus->rxlen) {
        brcmf_sdio_dcmd_resp_wake(bus);
    }
}

/* Pad read to blocksize for efficiency */
static void brcmf_sdio_pad(struct brcmf_sdio* bus, uint16_t* pad, uint16_t* rdlen) {
    if (bus->roundup && bus->blocksize && *rdlen > bus->blocksize) {
        *pad = bus->blocksize - (*rdlen % bus->blocksize);
        if (*pad <= bus->roundup && *pad < bus->blocksize &&
                *rdlen + *pad + BRCMF_FIRSTREAD < MAX_RX_DATASZ) {
            *rdlen += *pad;
        }
    } else if (*rdlen % bus->head_align) {
        *rdlen += bus->head_align - (*rdlen % bus->head_align);
    }
}

static uint brcmf_sdio_readframes(struct brcmf_sdio* bus, uint maxframes) {
    struct brcmf_netbuf* pkt; /* Packet for event or data frames */
    uint16_t pad;             /* Number of pad bytes to read */
    uint rxleft = 0;     /* Remaining number of frames allowed */
    zx_status_t ret;             /* Return code from calls */
    uint rxcount = 0;    /* Total frames read */
    struct brcmf_sdio_hdrinfo* rd = &bus->cur_read;
    struct brcmf_sdio_hdrinfo rd_new;
    uint8_t head_read = 0;

    brcmf_dbg(TRACE, "Enter\n");

    /* Not finished unless we encounter no more frames indication */
    bus->rxpending = true;

    for (rd->seq_num = bus->rx_seq, rxleft = maxframes;
            !bus->rxskip && rxleft && bus->sdiodev->state == BRCMF_SDIOD_DATA;
            rd->seq_num++, rxleft--) {
        /* Handle glomming separately */
        if (bus->glomd || !brcmf_netbuf_list_is_empty(&bus->glom)) {
            uint8_t cnt;
            brcmf_dbg(GLOM, "calling rxglom: glomd %p, glom %p\n", bus->glomd,
                      brcmf_netbuf_list_peek_head(&bus->glom));
            cnt = brcmf_sdio_rxglom(bus, rd->seq_num);
            brcmf_dbg(GLOM, "rxglom returned %d\n", cnt);
            rd->seq_num += cnt - 1;
            rxleft = (rxleft > cnt) ? (rxleft - cnt) : 1;
            continue;
        }

        rd->len_left = rd->len;
        /* read header first for unknow frame length */
        sdio_claim_host(bus->sdiodev->func1);
        if (!rd->len) {
            ret = brcmf_sdiod_recv_buf(bus->sdiodev, bus->rxhdr, BRCMF_FIRSTREAD);
            bus->sdcnt.f2rxhdrs++;
            if (ret != ZX_OK) {
                brcmf_err("RXHEADER FAILED: %d\n", ret);
                bus->sdcnt.rx_hdrfail++;
                brcmf_sdio_rxfail(bus, true, true);
                sdio_release_host(bus->sdiodev->func1);
                continue;
            }

            brcmf_dbg_hex_dump(BRCMF_BYTES_ON() || BRCMF_HDRS_ON(), bus->rxhdr, SDPCM_HDRLEN,
                               "RxHdr:\n");

            if (brcmf_sdio_hdparse(bus, bus->rxhdr, rd, BRCMF_SDIO_FT_NORMAL) != ZX_OK) {
                sdio_release_host(bus->sdiodev->func1);
                if (!bus->rxpending) {
                    break;
                } else {
                    continue;
                }
            }

            if (rd->channel == SDPCM_CONTROL_CHANNEL) {
                brcmf_sdio_read_control(bus, bus->rxhdr, rd->len, rd->dat_offset);
                /* prepare the descriptor for the next read */
                rd->len = rd->len_nxtfrm << 4;
                rd->len_nxtfrm = 0;
                /* treat all packet as event if we don't know */
                rd->channel = SDPCM_EVENT_CHANNEL;
                sdio_release_host(bus->sdiodev->func1);
                continue;
            }
            rd->len_left = rd->len > BRCMF_FIRSTREAD ? rd->len - BRCMF_FIRSTREAD : 0;
            head_read = BRCMF_FIRSTREAD;
        }

        brcmf_sdio_pad(bus, &pad, &rd->len_left);

        pkt = brcmu_pkt_buf_get_netbuf(rd->len_left + head_read + bus->head_align);
        if (!pkt) {
            /* Give up on data, request rtx of events */
            brcmf_err("brcmu_pkt_buf_get_netbuf failed\n");
            brcmf_sdio_rxfail(bus, false, RETRYCHAN(rd->channel));
            sdio_release_host(bus->sdiodev->func1);
            continue;
        }
        brcmf_netbuf_shrink_head(pkt, head_read);
        pkt_align(pkt, rd->len_left, bus->head_align);

        ret = brcmf_sdiod_recv_pkt(bus->sdiodev, pkt);
        bus->sdcnt.f2rxdata++;
        sdio_release_host(bus->sdiodev->func1);

        if (ret != ZX_OK) {
            brcmf_err("read %d bytes from channel %d failed: %d\n", rd->len, rd->channel, ret);
            brcmu_pkt_buf_free_netbuf(pkt);
            sdio_claim_host(bus->sdiodev->func1);
            brcmf_sdio_rxfail(bus, true, RETRYCHAN(rd->channel));
            sdio_release_host(bus->sdiodev->func1);
            continue;
        }

        if (head_read) {
            brcmf_netbuf_grow_head(pkt, head_read);
            memcpy(pkt->data, bus->rxhdr, head_read);
            head_read = 0;
        } else {
            memcpy(bus->rxhdr, pkt->data, SDPCM_HDRLEN);
            rd_new.seq_num = rd->seq_num;
            sdio_claim_host(bus->sdiodev->func1);
            if (brcmf_sdio_hdparse(bus, bus->rxhdr, &rd_new, BRCMF_SDIO_FT_NORMAL) != ZX_OK) {
                rd->len = 0;
                brcmu_pkt_buf_free_netbuf(pkt);
            }
            bus->sdcnt.rx_readahead_cnt++;
            if (rd->len != roundup(rd_new.len, 16)) {
                brcmf_err("frame length mismatch:read %d, should be %d\n", rd->len,
                          roundup(rd_new.len, 16) >> 4);
                rd->len = 0;
                brcmf_sdio_rxfail(bus, true, true);
                sdio_release_host(bus->sdiodev->func1);
                brcmu_pkt_buf_free_netbuf(pkt);
                continue;
            }
            sdio_release_host(bus->sdiodev->func1);
            rd->len_nxtfrm = rd_new.len_nxtfrm;
            rd->channel = rd_new.channel;
            rd->dat_offset = rd_new.dat_offset;

            brcmf_dbg_hex_dump(!(BRCMF_BYTES_ON() && BRCMF_DATA_ON()) && BRCMF_HDRS_ON(),
                               bus->rxhdr, SDPCM_HDRLEN, "RxHdr:\n");

            if (rd_new.channel == SDPCM_CONTROL_CHANNEL) {
                brcmf_err("readahead on control packet %d?\n", rd_new.seq_num);
                /* Force retry w/normal header read */
                rd->len = 0;
                sdio_claim_host(bus->sdiodev->func1);
                brcmf_sdio_rxfail(bus, false, true);
                sdio_release_host(bus->sdiodev->func1);
                brcmu_pkt_buf_free_netbuf(pkt);
                continue;
            }
        }

        brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_DATA_ON(), pkt->data, rd->len, "Rx Data:\n");

        /* Save superframe descriptor and allocate packet frame */
        if (rd->channel == SDPCM_GLOM_CHANNEL) {
            if (SDPCM_GLOMDESC(&bus->rxhdr[SDPCM_HWHDR_LEN])) {
                brcmf_dbg(GLOM, "glom descriptor, %d bytes:\n", rd->len);
                brcmf_dbg_hex_dump(BRCMF_GLOM_ON(), pkt->data, rd->len, "Glom Data:\n");
                brcmf_netbuf_set_length_to(pkt, rd->len);
                brcmf_netbuf_shrink_head(pkt, SDPCM_HDRLEN);
                bus->glomd = pkt;
            } else {
                brcmf_err(
                    "%s: glom superframe w/o "
                    "descriptor!\n",
                    __func__);
                sdio_claim_host(bus->sdiodev->func1);
                brcmf_sdio_rxfail(bus, false, false);
                sdio_release_host(bus->sdiodev->func1);
            }
            /* prepare the descriptor for the next read */
            rd->len = rd->len_nxtfrm << 4;
            rd->len_nxtfrm = 0;
            /* treat all packet as event if we don't know */
            rd->channel = SDPCM_EVENT_CHANNEL;
            continue;
        }

        /* Fill in packet len and prio, deliver upward */
        brcmf_netbuf_set_length_to(pkt, rd->len);
        brcmf_netbuf_shrink_head(pkt, rd->dat_offset);

        if (pkt->len == 0) {
            brcmu_pkt_buf_free_netbuf(pkt);
        } else if (rd->channel == SDPCM_EVENT_CHANNEL) {
            brcmf_rx_event(&bus->sdiodev->dev, pkt);
        } else {
            brcmf_rx_frame(&bus->sdiodev->dev, pkt, false);
        }

        /* prepare the descriptor for the next read */
        rd->len = rd->len_nxtfrm << 4;
        rd->len_nxtfrm = 0;
        /* treat all packet as event if we don't know */
        rd->channel = SDPCM_EVENT_CHANNEL;
    }

    rxcount = maxframes - rxleft;
    /* Message if we hit the limit */
    if (!rxleft) {
        brcmf_dbg(DATA, "hit rx limit of %d frames\n", maxframes);
    } else {
        brcmf_dbg(DATA, "processed %d frames\n", rxcount);
    }
    /* Back off rxseq if awaiting rtx, update rx_seq */
    if (bus->rxskip) {
        rd->seq_num--;
    }
    bus->rx_seq = rd->seq_num;

    return rxcount;
}

static void brcmf_sdio_wait_event_wakeup(struct brcmf_sdio* bus) {
    sync_completion_signal(&bus->ctrl_wait);
    return;
}

static zx_status_t brcmf_sdio_txpkt_hdalign(struct brcmf_sdio* bus, struct brcmf_netbuf* pkt,
                                            uint16_t* head_pad_out) {
    struct brcmf_bus_stats* stats;
    uint16_t head_pad;
    uint8_t* dat_buf;

    dat_buf = (uint8_t*)(pkt->data);

    /* Check head padding */
    head_pad = ((unsigned long)dat_buf % bus->head_align);
    if (head_pad) {
        if (brcmf_netbuf_head_space(pkt) < head_pad) {
            stats = &bus->sdiodev->bus_if->stats;
            atomic_fetch_add(&stats->pktcowed, 1);
            if (brcmf_netbuf_grow_realloc(pkt, head_pad, 0)) {
                atomic_fetch_add(&stats->pktcow_failed, 1);
                return ZX_ERR_NO_MEMORY;
            }
            head_pad = 0;
        }
        brcmf_netbuf_grow_head(pkt, head_pad);
        dat_buf = (uint8_t*)(pkt->data);
    }
    memset(dat_buf, 0, head_pad + bus->tx_hdrlen);
    if (head_pad_out) {
        *head_pad_out = head_pad;
    }
    return ZX_OK;
}

/*
 * struct brcmf_netbuf_workspace reserves the first two bytes in brcmf_netbuf.workspace for
 * bus layer usage.
 */
/* flag marking a dummy netbuf added for DMA alignment requirement */
#define ALIGN_NETBUF_FLAG 0x8000
/* bit mask of data length chopped from the previous packet */
#define ALIGN_NETBUF_CHOP_LEN_MASK 0x7fff

static zx_status_t brcmf_sdio_txpkt_prep_sg(struct brcmf_sdio* bus, struct brcmf_netbuf_list* pktq,
                                            struct brcmf_netbuf* pkt, uint16_t total_len,
                                            uint16_t* tail_pad_out) {
    struct brcmf_sdio_dev* sdiodev;
    struct brcmf_netbuf* pkt_pad;
    uint16_t tail_pad, tail_chop, chain_pad, head_pad;
    uint16_t blksize;
    bool lastfrm;
    int ntail;
    zx_status_t ret;

    sdiodev = bus->sdiodev;
    sdio_get_block_size(sdiodev->sdio_proto, SDIO_FN_2, &blksize);
    /* sg entry alignment should be a divisor of block size */
    WARN_ON(blksize % bus->sgentry_align);

    /* Check tail padding */
    lastfrm = brcmf_netbuf_list_peek_tail(pktq) == pkt;
    tail_pad = 0;
    tail_chop = pkt->len % bus->sgentry_align;
    if (tail_chop) {
        tail_pad = bus->sgentry_align - tail_chop;
    }
    chain_pad = (total_len + tail_pad) % blksize;
    if (lastfrm && chain_pad) {
        tail_pad += blksize - chain_pad;
    }
    if (brcmf_netbuf_tail_space(pkt) < tail_pad && pkt->len > blksize) {
        pkt_pad = brcmu_pkt_buf_get_netbuf(tail_pad + tail_chop + bus->head_align);
        if (pkt_pad == NULL) {
            return ZX_ERR_NO_MEMORY;
        }
        ret = brcmf_sdio_txpkt_hdalign(bus, pkt_pad, &head_pad);
        if (unlikely(ret != ZX_OK)) {
            brcmf_netbuf_free(pkt_pad);
            return ret;
        }
        memcpy(pkt_pad->data, pkt->data + pkt->len - tail_chop, tail_chop);
        *(uint16_t*)(pkt_pad->workspace) = ALIGN_NETBUF_FLAG + tail_chop;
        brcmf_netbuf_reduce_length_to(pkt, pkt->len - tail_chop);
        brcmf_netbuf_reduce_length_to(pkt_pad, tail_pad + tail_chop);
        brcmf_netbuf_list_add_after(pktq, pkt, pkt_pad);
    } else {
        ntail = tail_pad - brcmf_netbuf_tail_space(pkt);
        if (ntail > 0)
            if (brcmf_netbuf_grow_realloc(pkt, 0, ntail)) {
                return ZX_ERR_NO_MEMORY;
            }
        brcmf_netbuf_grow_tail(pkt, tail_pad);
    }

    if (tail_pad_out) {
        *tail_pad_out = tail_pad;
    }
    return ZX_OK;
}

/**
 * brcmf_sdio_txpkt_prep - packet preparation for transmit
 * @bus: brcmf_sdio structure pointer
 * @pktq: packet list pointer
 * @chan: virtual channel to transmit the packet
 *
 * Processes to be applied to the packet
 *  - Align data buffer pointer
 *  - Align data buffer length
 *  - Prepare header
 * Return: negative value if there is error
 */
static zx_status_t brcmf_sdio_txpkt_prep(struct brcmf_sdio* bus, struct brcmf_netbuf_list* pktq,
                                         uint chan) {
    uint16_t head_pad, total_len;
    struct brcmf_netbuf* pkt_next;
    uint8_t txseq;
    zx_status_t ret;
    struct brcmf_sdio_hdrinfo hd_info = {0};

    txseq = bus->tx_seq;
    total_len = 0;
    brcmf_netbuf_list_for_every(pktq, pkt_next) {
        /* alignment packet inserted in previous
         * loop cycle can be skipped as it is
         * already properly aligned and does not
         * need an sdpcm header.
         */
        if (*(uint16_t*)(pkt_next->workspace) & ALIGN_NETBUF_FLAG) {
            continue;
        }

        /* align packet data pointer */
        ret = brcmf_sdio_txpkt_hdalign(bus, pkt_next, &head_pad);
        if (ret != ZX_OK) {
            return ret;
        }
        if (head_pad) {
            memset(pkt_next->data + bus->tx_hdrlen, 0, head_pad);
        }

        total_len += pkt_next->len;

        hd_info.len = pkt_next->len;
        hd_info.lastfrm = brcmf_netbuf_list_peek_tail(pktq) == pkt_next;
        if (bus->txglom && brcmf_netbuf_list_length(pktq) > 1) {
            ret = brcmf_sdio_txpkt_prep_sg(bus, pktq, pkt_next, total_len, &hd_info.tail_pad);
            if (ret != ZX_OK) {
                return ret;
            }
            total_len += hd_info.tail_pad;
        }

        hd_info.channel = chan;
        hd_info.dat_offset = head_pad + bus->tx_hdrlen;
        hd_info.seq_num = txseq++;

        /* Now fill the header */
        brcmf_sdio_hdpack(bus, pkt_next->data, &hd_info);

        if (BRCMF_BYTES_ON() && ((BRCMF_CTL_ON() && chan == SDPCM_CONTROL_CHANNEL) ||
                                 (BRCMF_DATA_ON() && chan != SDPCM_CONTROL_CHANNEL))) {
            brcmf_dbg_hex_dump(true, pkt_next->data, hd_info.len, "Tx Frame:\n");
        } else if (BRCMF_HDRS_ON()) {
            brcmf_dbg_hex_dump(true, pkt_next->data, head_pad + bus->tx_hdrlen, "Tx Header:\n");
        }
    }
    /* Hardware length tag of the first packet should be total
     * length of the chain (including padding)
     */
    if (bus->txglom) {
        brcmf_sdio_update_hwhdr(brcmf_netbuf_list_peek_head(pktq)->data, total_len);
    }
    return ZX_OK;
}

/**
 * brcmf_sdio_txpkt_postp - packet post processing for transmit
 * @bus: brcmf_sdio structure pointer
 * @pktq: packet list pointer
 *
 * Processes to be applied to the packet
 *  - Remove head padding
 *  - Remove tail padding
 */
static void brcmf_sdio_txpkt_postp(struct brcmf_sdio* bus, struct brcmf_netbuf_list* pktq) {
    uint8_t* hdr;
    uint32_t dat_offset;
    uint16_t tail_pad;
    uint16_t dummy_flags, chop_len;
    struct brcmf_netbuf* pkt_next;
    struct brcmf_netbuf* tmp;
    struct brcmf_netbuf* pkt_prev;

    brcmf_netbuf_list_for_every_safe(pktq, pkt_next, tmp) {
        dummy_flags = *(uint16_t*)(pkt_next->workspace);
        if (dummy_flags & ALIGN_NETBUF_FLAG) {
            chop_len = dummy_flags & ALIGN_NETBUF_CHOP_LEN_MASK;
            if (chop_len) {
                pkt_prev = brcmf_netbuf_list_prev(pktq, pkt_next);
                brcmf_netbuf_grow_tail(pkt_prev, chop_len);
            }
            brcmf_netbuf_list_remove(pktq, pkt_next);
            brcmu_pkt_buf_free_netbuf(pkt_next);
        } else {
            hdr = pkt_next->data + bus->tx_hdrlen - SDPCM_SWHDR_LEN;
            dat_offset = *(uint32_t*)hdr;
            dat_offset = (dat_offset & SDPCM_DOFFSET_MASK) >> SDPCM_DOFFSET_SHIFT;
            brcmf_netbuf_shrink_head(pkt_next, dat_offset);
            if (bus->txglom) {
                tail_pad = *(uint16_t*)(hdr - 2);
                brcmf_netbuf_reduce_length_to(pkt_next, pkt_next->len - tail_pad);
            }
        }
    }
}

/* Writes a HW/SW header into the packet and sends it. */
/* Assumes: (a) header space already there, (b) caller holds lock */
static zx_status_t brcmf_sdio_txpkt(struct brcmf_sdio* bus, struct brcmf_netbuf_list* pktq,
                                    uint chan) {
    zx_status_t ret;
    struct brcmf_netbuf* pkt_next;
    struct brcmf_netbuf* tmp;

    brcmf_dbg(TRACE, "Enter\n");

    ret = brcmf_sdio_txpkt_prep(bus, pktq, chan);
    if (ret != ZX_OK) {
        goto done;
    }

    sdio_claim_host(bus->sdiodev->func1);
    ret = brcmf_sdiod_send_pkt(bus->sdiodev, pktq);
    bus->sdcnt.f2txdata++;

    if (ret != ZX_OK) {
        brcmf_sdio_txfail(bus);
    }

    sdio_release_host(bus->sdiodev->func1);

done:
    brcmf_sdio_txpkt_postp(bus, pktq);
    if (ret == ZX_OK) {
        bus->tx_seq = (bus->tx_seq + brcmf_netbuf_list_length(pktq)) % SDPCM_SEQ_WRAP;
    }
    brcmf_netbuf_list_for_every_safe(pktq, pkt_next, tmp) {
        brcmf_netbuf_list_remove(pktq, pkt_next);
        brcmf_proto_bcdc_txcomplete(&bus->sdiodev->dev, pkt_next, ret == ZX_OK);
    }
    return ret;
}

static uint brcmf_sdio_sendfromq(struct brcmf_sdio* bus, uint maxframes) {
    struct brcmf_netbuf* pkt;
    struct brcmf_netbuf_list pktq;
    uint32_t intstat_addr = bus->sdio_core->base + SD_REG(intstatus);
    uint32_t intstatus = 0;
    zx_status_t ret = ZX_OK;
    int prec_out, i;
    uint cnt = 0;
    uint8_t tx_prec_map, pkt_num;

    brcmf_dbg(TRACE, "Enter\n");

    tx_prec_map = ~bus->flowcontrol;

    /* Send frames until the limit or some other event */
    for (cnt = 0; (cnt < maxframes) && data_ok(bus);) {
        pkt_num = 1;
        if (bus->txglom) {
            pkt_num = min_t(uint8_t, bus->tx_max - bus->tx_seq, bus->sdiodev->txglomsz);
        }
        pkt_num = min_t(uint32_t, pkt_num, brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol));
        brcmf_netbuf_list_init(&pktq);
        //spin_lock_bh(&bus->txq_lock);
        pthread_mutex_lock(&irq_callback_lock);
        for (i = 0; i < pkt_num; i++) {
            pkt = brcmu_pktq_mdeq(&bus->txq, tx_prec_map, &prec_out);
            if (pkt == NULL) {
                break;
            }
            brcmf_netbuf_list_add_tail(&pktq, pkt);
        }
        //spin_unlock_bh(&bus->txq_lock);
        pthread_mutex_unlock(&irq_callback_lock);
        if (i == 0) {
            break;
        }

        ret = brcmf_sdio_txpkt(bus, &pktq, SDPCM_DATA_CHANNEL);

        cnt += i;

        /* In poll mode, need to check for other events */
        if (!bus->intr) {
            /* Check device status, signal pending interrupt */
            sdio_claim_host(bus->sdiodev->func1);
            intstatus = brcmf_sdiod_readl(bus->sdiodev, intstat_addr, &ret);
            sdio_release_host(bus->sdiodev->func1);

            bus->sdcnt.f2txdata++;
            if (ret != ZX_OK) {
                break;
            }
            if (intstatus & bus->hostintmask) {
                atomic_store(&bus->ipend, 1);
            }
        }
    }

    /* Deflow-control stack if needed */
    if ((bus->sdiodev->state == BRCMF_SDIOD_DATA) && bus->txoff && (pktq_len(&bus->txq) < TXLOW)) {
        bus->txoff = false;
        brcmf_proto_bcdc_txflowblock(&bus->sdiodev->dev, false);
    }

    return cnt;
}

static zx_status_t brcmf_sdio_tx_ctrlframe(struct brcmf_sdio* bus, uint8_t* frame, uint16_t len) {
    uint8_t doff;
    uint16_t pad;
    uint retries = 0;
    struct brcmf_sdio_hdrinfo hd_info = {0};
    // TODO(cphoenix): ret, err, rv, error, status - more consistency is better.
    zx_status_t ret;

    brcmf_dbg(TRACE, "Enter\n");

    /* Back the pointer to make room for bus header */
    frame -= bus->tx_hdrlen;
    len += bus->tx_hdrlen;

    /* Add alignment padding (optional for ctl frames) */
    doff = ((unsigned long)frame % bus->head_align);
    if (doff) {
        frame -= doff;
        len += doff;
        memset(frame + bus->tx_hdrlen, 0, doff);
    }

    /* Round send length to next SDIO block */
    pad = 0;
    if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
        pad = bus->blocksize - (len % bus->blocksize);
        if ((pad > bus->roundup) || (pad >= bus->blocksize)) {
            pad = 0;
        }
    } else if (len % bus->head_align) {
        pad = bus->head_align - (len % bus->head_align);
    }
    len += pad;

    hd_info.len = len - pad;
    hd_info.channel = SDPCM_CONTROL_CHANNEL;
    hd_info.dat_offset = doff + bus->tx_hdrlen;
    hd_info.seq_num = bus->tx_seq;
    hd_info.lastfrm = true;
    hd_info.tail_pad = pad;
    brcmf_sdio_hdpack(bus, frame, &hd_info);

    if (bus->txglom) {
        brcmf_sdio_update_hwhdr(frame, len);
    }

    brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_CTL_ON(), frame, len, "Tx Frame:\n");
    brcmf_dbg_hex_dump(!(BRCMF_BYTES_ON() && BRCMF_CTL_ON()) && BRCMF_HDRS_ON(), frame,
                       min_t(uint16_t, len, 16), "TxHdr:\n");

    do {
        ret = brcmf_sdiod_send_buf(bus->sdiodev, frame, len);

        if (ret != ZX_OK) {
            brcmf_sdio_txfail(bus);
        } else {
            bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQ_WRAP;
        }
    } while (ret != ZX_OK && retries++ < TXRETRIES);

    return ret;
}

static void brcmf_sdio_bus_stop(struct brcmf_device* dev) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;
    struct brcmf_core* core = bus->sdio_core;
    uint32_t local_hostintmask;
    uint8_t saveclk;
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter\n");

    if (bus->watchdog_tsk) {
        send_sig(SIGTERM, bus->watchdog_tsk, 1);
        kthread_stop(bus->watchdog_tsk);
        bus->watchdog_tsk = NULL;
    }

    if (sdiodev->state != BRCMF_SDIOD_NOMEDIUM) {
        sdio_claim_host(sdiodev->func1);

        /* Enable clock for device interrupts */
        brcmf_sdio_bus_sleep(bus, false, false);

        /* Disable and clear interrupts at the chip level also */
        brcmf_sdiod_writel(sdiodev, core->base + SD_REG(hostintmask), 0, NULL);

        local_hostintmask = bus->hostintmask;
        bus->hostintmask = 0;

        /* Force backplane clocks to assure F2 interrupt propagates */
        saveclk = brcmf_sdiod_readb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
        if (err == ZX_OK) {
            brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, (saveclk | SBSDIO_FORCE_HT), &err);
        }
        if (err != ZX_OK) {
            brcmf_err("Failed to force clock for F2: err %s\n", zx_status_get_string(err));
        }

        /* Turn off the bus (F2), free any pending packets */
        brcmf_dbg(INTR, "disable SDIO interrupts\n");
        sdio_disable_fn(sdiodev->sdio_proto, SDIO_FN_2);

        /* Clear any pending interrupts now that F2 is disabled */
        brcmf_sdiod_writel(sdiodev, core->base + SD_REG(intstatus), local_hostintmask, NULL);

        sdio_release_host(sdiodev->func1);
    }
    /* Clear the data packet queues */
    brcmu_pktq_flush(&bus->txq, true, NULL, NULL);

    /* Clear any held glomming stuff */
    brcmu_pkt_buf_free_netbuf(bus->glomd);
    brcmf_sdio_free_glom(bus);

    /* Clear rx control and wake any waiters */
    //spin_lock_bh(&bus->rxctl_lock);
    pthread_mutex_lock(&irq_callback_lock);
    bus->rxlen = 0;
    //spin_unlock_bh(&bus->rxctl_lock);
    pthread_mutex_unlock(&irq_callback_lock);
    // TODO(cphoenix): I think the original Linux code in brcmf_sdio_dcmd_resp_wait() would have
    // gone right back to sleep, since rxlen is 0. In the current code, it will exit;
    // brcmf_sdio_bus_rxctl() will return ZX_ERR_SHOULD_WAIT; the loop in brcmf_proto_bcdc_cmplt
    // will terminate. Check once we're supporting SDIO: Is this what we want? Why was this an
    // apparent NOP in Linux?
    brcmf_sdio_dcmd_resp_wake(bus);

    /* Reset some F2 state stuff */
    bus->rxskip = false;
    bus->tx_seq = bus->rx_seq = 0;
}

static inline void brcmf_sdio_clrintr(struct brcmf_sdio* bus) {
    struct brcmf_sdio_dev* sdiodev;

    sdiodev = bus->sdiodev;
    if (sdiodev->oob_irq_requested) {
        //spin_lock_irqsave(&sdiodev->irq_en_lock, flags);
        pthread_mutex_lock(&irq_callback_lock);
        if (!sdiodev->irq_en && !atomic_load(&bus->ipend)) {
            sdiodev->irq_en = true;
        }
        //spin_unlock_irqrestore(&sdiodev->irq_en_lock, flags);
        pthread_mutex_unlock(&irq_callback_lock);
    }
}

static zx_status_t brcmf_sdio_intr_rstatus(struct brcmf_sdio* bus) {
    struct brcmf_core* core = bus->sdio_core;
    uint32_t addr;
    unsigned long val;
    zx_status_t ret;

    addr = core->base + SD_REG(intstatus);

    val = brcmf_sdiod_readl(bus->sdiodev, addr, &ret);
    bus->sdcnt.f1regdata++;
    if (ret != ZX_OK) {
        return ret;
    }

    val &= bus->hostintmask;
    atomic_store(&bus->fcstate, !!(val & I_HMB_FC_STATE));

    /* Clear interrupts */
    if (val) {
        brcmf_sdiod_writel(bus->sdiodev, addr, val, &ret);
        bus->sdcnt.f1regdata++;
        atomic_fetch_or(&bus->intstatus, val);
    }

    return ret;
}

static void brcmf_sdio_dpc(struct brcmf_sdio* bus) {
    struct brcmf_sdio_dev* sdiod = bus->sdiodev;
    uint32_t newstatus = 0;
    uint32_t intstat_addr = bus->sdio_core->base + SD_REG(intstatus);
    unsigned long intstatus;
    uint txlimit = bus->txbound; /* Tx frames to send before resched */
    uint framecnt;               /* Temporary counter of tx/rx frames */
    zx_status_t err = ZX_OK;

    brcmf_dbg(TRACE, "Enter\n");

    sdio_claim_host(bus->sdiodev->func1);

    /* If waiting for HTAVAIL, check status */
    if (!bus->sr_enabled && bus->clkstate == CLK_PENDING) {
        uint8_t clkctl;
        uint8_t devctl = 0;

#ifdef DEBUG
        /* Check for inconsistent device control */
        devctl = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
#endif /* DEBUG */

        /* Read CSR, if clock on switch to AVAIL, else ignore */
        clkctl = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);

        brcmf_dbg(SDIO, "DPC: PENDING, devctl 0x%02x clkctl 0x%02x\n", devctl, clkctl);

        if (SBSDIO_HTAV(clkctl)) {
            devctl = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
            devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
            brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_DEVICE_CTL, devctl, &err);
            bus->clkstate = CLK_AVAIL;
        }
    }

    /* Make sure backplane clock is on */
    brcmf_sdio_bus_sleep(bus, false, true);

    /* Pending interrupt indicates new device status */
    if (atomic_load(&bus->ipend) > 0) {
        atomic_store(&bus->ipend, 0);
        err = brcmf_sdio_intr_rstatus(bus);
    }

    /* Start with leftover status bits */
    intstatus = atomic_exchange(&bus->intstatus, 0);

    /* Handle flow-control change: read new state in case our ack
     * crossed another change interrupt.  If change still set, assume
     * FC ON for safety, let next loop through do the debounce.
     */
    if (intstatus & I_HMB_FC_CHANGE) {
        intstatus &= ~I_HMB_FC_CHANGE;
        brcmf_sdiod_writel(sdiod, intstat_addr, I_HMB_FC_CHANGE, &err);

        newstatus = brcmf_sdiod_readl(sdiod, intstat_addr, &err);

        bus->sdcnt.f1regdata += 2;
        atomic_store(&bus->fcstate, !!(newstatus & (I_HMB_FC_STATE | I_HMB_FC_CHANGE)));
        intstatus |= (newstatus & bus->hostintmask);
    }

    /* Handle host mailbox indication */
    if (intstatus & I_HMB_HOST_INT) {
        intstatus &= ~I_HMB_HOST_INT;
        intstatus |= brcmf_sdio_hostmail(bus);
    }

    sdio_release_host(bus->sdiodev->func1);

    /* Generally don't ask for these, can get CRC errors... */
    if (intstatus & I_WR_OOSYNC) {
        brcmf_err("Dongle reports WR_OOSYNC\n");
        intstatus &= ~I_WR_OOSYNC;
    }

    if (intstatus & I_RD_OOSYNC) {
        brcmf_err("Dongle reports RD_OOSYNC\n");
        intstatus &= ~I_RD_OOSYNC;
    }

    if (intstatus & I_SBINT) {
        brcmf_err("Dongle reports SBINT\n");
        intstatus &= ~I_SBINT;
    }

    /* Would be active due to wake-wlan in gSPI */
    if (intstatus & I_CHIPACTIVE) {
        brcmf_dbg(INFO, "Dongle reports CHIPACTIVE\n");
        intstatus &= ~I_CHIPACTIVE;
    }

    /* Ignore frame indications if rxskip is set */
    if (bus->rxskip) {
        intstatus &= ~I_HMB_FRAME_IND;
    }

    /* On frame indication, read available frames */
    if ((intstatus & I_HMB_FRAME_IND) && (bus->clkstate == CLK_AVAIL)) {
        brcmf_sdio_readframes(bus, bus->rxbound);
        if (!bus->rxpending) {
            intstatus &= ~I_HMB_FRAME_IND;
        }
    }

    /* Keep still-pending events for next scheduling */
    if (intstatus) {
        atomic_fetch_or(&bus->intstatus, intstatus);
    }

    brcmf_sdio_clrintr(bus);

    if (bus->ctrl_frame_stat && (bus->clkstate == CLK_AVAIL) && data_ok(bus)) {
        sdio_claim_host(bus->sdiodev->func1);
        if (bus->ctrl_frame_stat) {
            err = brcmf_sdio_tx_ctrlframe(bus, bus->ctrl_frame_buf, bus->ctrl_frame_len);
            bus->ctrl_frame_err = err;
            wmb();
            bus->ctrl_frame_stat = false;
        }
        sdio_release_host(bus->sdiodev->func1);
        brcmf_sdio_wait_event_wakeup(bus);
    }
    /* Send queued frames (limit 1 if rx may still be pending) */
    if ((bus->clkstate == CLK_AVAIL) && !atomic_load(&bus->fcstate) &&
            brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol) && txlimit && data_ok(bus)) {
        framecnt = bus->rxpending ? min(txlimit, bus->txminmax) : txlimit;
        brcmf_sdio_sendfromq(bus, framecnt);
    }

    if ((bus->sdiodev->state != BRCMF_SDIOD_DATA) || (err != ZX_OK)) {
        brcmf_err("failed backplane access over SDIO, halting operation\n");
        atomic_store(&bus->intstatus, 0);
        if (bus->ctrl_frame_stat) {
            sdio_claim_host(bus->sdiodev->func1);
            if (bus->ctrl_frame_stat) {
                bus->ctrl_frame_err = ZX_ERR_IO_REFUSED;
                wmb();
                bus->ctrl_frame_stat = false;
                brcmf_sdio_wait_event_wakeup(bus);
            }
            sdio_release_host(bus->sdiodev->func1);
        }
    } else if (atomic_load(&bus->intstatus) || atomic_load(&bus->ipend) > 0 ||
               (!atomic_load(&bus->fcstate) && brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol) &&
                data_ok(bus))) {
        bus->dpc_triggered = true;
    }
}

static struct pktq* brcmf_sdio_bus_gettxq(struct brcmf_device* dev) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;

    return &bus->txq;
}

static bool brcmf_sdio_prec_enq(struct pktq* q, struct brcmf_netbuf* pkt, int prec) {
    struct brcmf_netbuf* p;
    int eprec = -1; /* precedence to evict from */

    /* Fast case, precedence queue is not full and we are also not
     * exceeding total queue length
     */
    if (!pktq_pfull(q, prec) && !pktq_full(q)) {
        brcmu_pktq_penq(q, prec, pkt);
        return true;
    }

    /* Determine precedence from which to evict packet, if any */
    if (pktq_pfull(q, prec)) {
        eprec = prec;
    } else if (pktq_full(q)) {
        p = brcmu_pktq_peek_tail(q, &eprec);
        if (eprec > prec) {
            return false;
        }
    }

    /* Evict if needed */
    if (eprec >= 0) {
        /* Detect queueing to unconfigured precedence */
        if (eprec == prec) {
            return false;    /* refuse newer (incoming) packet */
        }
        /* Evict packet according to discard policy */
        p = brcmu_pktq_pdeq_tail(q, eprec);
        if (p == NULL) {
            brcmf_err("brcmu_pktq_pdeq_tail() failed\n");
        }
        brcmu_pkt_buf_free_netbuf(p);
    }

    /* Enqueue */
    p = brcmu_pktq_penq(q, prec, pkt);
    if (p == NULL) {
        brcmf_err("brcmu_pktq_penq() failed\n");
    }

    return p != NULL;
}

static zx_status_t brcmf_sdio_bus_txdata(struct brcmf_device* dev, struct brcmf_netbuf* pkt) {
    zx_status_t ret;
    uint prec;
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;

    brcmf_dbg(TRACE, "Enter: pkt: data %p len %d\n", pkt->data, pkt->len);
    if (sdiodev->state != BRCMF_SDIOD_DATA) {
        return ZX_ERR_IO;
    }

    /* Add space for the header */
    brcmf_netbuf_grow_head(pkt, bus->tx_hdrlen);
    /* precondition: IS_ALIGNED((unsigned long)(pkt->data), 2) */

    prec = prio2prec((pkt->priority & PRIOMASK));

    /* Check for existing queue, current flow-control,
                     pending event, or pending clock */
    brcmf_dbg(TRACE, "deferring pktq len %d\n", pktq_len(&bus->txq));
    bus->sdcnt.fcqueued++;

    /* Priority based enq */
    //spin_lock_bh(&bus->txq_lock);
    pthread_mutex_lock(&irq_callback_lock);
    /* reset bus_flags in packet workspace */
    *(uint16_t*)(pkt->workspace) = 0;
    if (!brcmf_sdio_prec_enq(&bus->txq, pkt, prec)) {
        brcmf_netbuf_shrink_head(pkt, bus->tx_hdrlen);
        brcmf_err("out of bus->txq !!!\n");
        ret = ZX_ERR_NO_RESOURCES;
    } else {
        ret = ZX_OK;
    }

    if (pktq_len(&bus->txq) >= TXHI) {
        bus->txoff = true;
        brcmf_proto_bcdc_txflowblock(dev, true);
    }
    //spin_unlock_bh(&bus->txq_lock);
    pthread_mutex_unlock(&irq_callback_lock);


#ifdef DEBUG
    if (pktq_plen(&bus->txq, prec) > qcount[prec]) {
        qcount[prec] = pktq_plen(&bus->txq, prec);
    }
#endif

    brcmf_sdio_trigger_dpc(bus);
    return ret;
}

#ifdef DEBUG
#define CONSOLE_LINE_MAX 192

static zx_status_t brcmf_sdio_readconsole(struct brcmf_sdio* bus) {
    struct brcmf_console* c = &bus->console;
    uint8_t line[CONSOLE_LINE_MAX];
    uint8_t ch;
    uint32_t n, idx, addr;
    zx_status_t rv;

    /* Don't do anything until FWREADY updates console address */
    if (bus->console_addr == 0) {
        return ZX_OK;
    }

    /* Read console log struct */
    addr = bus->console_addr + offsetof(struct rte_console, log_le);
    rv = brcmf_sdiod_ramrw(bus->sdiodev, false, addr, (uint8_t*)&c->log_le, sizeof(c->log_le));
    if (rv != ZX_OK) {
        return rv;
    }

    /* Allocate console buffer (one time only) */
    if (c->buf == NULL) {
        c->bufsize = c->log_le.buf_size;
        c->buf = malloc(c->bufsize);
        if (c->buf == NULL) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    idx = c->log_le.idx;

    /* Protect against corrupt value */
    if (idx > c->bufsize) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    /* Skip reading the console buffer if the index pointer
     has not moved */
    if (idx == c->last) {
        return ZX_OK;
    }

    /* Read the console buffer */
    addr = c->log_le.buf;
    rv = brcmf_sdiod_ramrw(bus->sdiodev, false, addr, c->buf, c->bufsize);
    if (rv != ZX_OK) {
        return rv;
    }

    while (c->last != idx) {
        for (n = 0; n < CONSOLE_LINE_MAX - 2; n++) {
            if (c->last == idx) {
                /* This would output a partial line.
                 * Instead, back up
                 * the buffer pointer and output this
                 * line next time around.
                 */
                if (c->last >= n) {
                    c->last -= n;
                } else {
                    c->last = c->bufsize - n;
                }
                goto break2;
            }
            ch = c->buf[c->last];
            c->last = (c->last + 1) % c->bufsize;
            if (ch == '\n') {
                break;
            }
            line[n] = ch;
        }

        if (n > 0) {
            if (line[n - 1] == '\r') {
                n--;
            }
            line[n] = 0;
            zxlogf(INFO, "brcmfmac: CONSOLE: %s\n", line);
        }
    }
break2:

    return ZX_OK;
}
#endif /* DEBUG */

static zx_status_t brcmf_sdio_bus_txctl(struct brcmf_device* dev, unsigned char* msg, uint msglen) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;
    zx_status_t ret;

    brcmf_dbg(TRACE, "Enter\n");
    if (sdiodev->state != BRCMF_SDIOD_DATA) {
        return ZX_ERR_IO;
    }

    /* Send from dpc */
    bus->ctrl_frame_buf = msg;
    bus->ctrl_frame_len = msglen;
    wmb();
    bus->ctrl_frame_stat = true;

    brcmf_sdio_trigger_dpc(bus);
    sync_completion_wait(&bus->ctrl_wait, ZX_MSEC(CTL_DONE_TIMEOUT_MSEC));
    ret = ZX_OK;
    if (bus->ctrl_frame_stat) {
        sdio_claim_host(bus->sdiodev->func1);
        if (bus->ctrl_frame_stat) {
            brcmf_dbg(SDIO, "ctrl_frame timeout\n");
            bus->ctrl_frame_stat = false;
            ret = ZX_ERR_SHOULD_WAIT;
        }
        sdio_release_host(bus->sdiodev->func1);
    }
    if (ret == ZX_OK) {
        brcmf_dbg(SDIO, "ctrl_frame complete, err=%d\n", bus->ctrl_frame_err);
        rmb();
        ret = bus->ctrl_frame_err;
    }

    if (ret != ZX_OK) {
        bus->sdcnt.tx_ctlerrs++;
    } else {
        bus->sdcnt.tx_ctlpkts++;
    }

    return ret;
}

#ifdef DEBUG
static zx_status_t brcmf_sdio_dump_console(struct seq_file* seq, struct brcmf_sdio* bus,
                                           struct sdpcm_shared* sh) {
    uint32_t addr, console_ptr, console_size, console_index;
    char* conbuf = NULL;
    uint32_t sh_val;
    zx_status_t rv;

    /* obtain console information from device memory */
    addr = sh->console_addr + offsetof(struct rte_console, log_le);
    rv = brcmf_sdiod_ramrw(bus->sdiodev, false, addr, (uint8_t*)&sh_val, sizeof(uint32_t));
    if (rv != ZX_OK) {
        return rv;
    }
    console_ptr = sh_val;

    addr = sh->console_addr + offsetof(struct rte_console, log_le.buf_size);
    rv = brcmf_sdiod_ramrw(bus->sdiodev, false, addr, (uint8_t*)&sh_val, sizeof(uint32_t));
    if (rv != ZX_OK) {
        return rv;
    }
    console_size = sh_val;

    addr = sh->console_addr + offsetof(struct rte_console, log_le.idx);
    rv = brcmf_sdiod_ramrw(bus->sdiodev, false, addr, (uint8_t*)&sh_val, sizeof(uint32_t));
    if (rv != ZX_OK) {
        return rv;
    }
    console_index = sh_val;

    /* allocate buffer for console data */
    if (console_size <= CONSOLE_BUFFER_MAX) {
        conbuf = calloc(1, console_size + 1);
    }

    if (!conbuf) {
        return ZX_ERR_NO_MEMORY;
    }

    /* obtain the console data from device */
    conbuf[console_size] = '\0';
    rv = brcmf_sdiod_ramrw(bus->sdiodev, false, console_ptr, (uint8_t*)conbuf, console_size);
    if (rv != ZX_OK) {
        goto done;
    }

    rv = seq_write(seq, conbuf + console_index, console_size - console_index);
    if (rv != ZX_OK) {
        goto done;
    }

    if (console_index > 0) {
        rv = seq_write(seq, conbuf, console_index - 1);
    }

done:
    free(conbuf);
    return rv;
}

static zx_status_t brcmf_sdio_trap_info(struct seq_file* seq, struct brcmf_sdio* bus,
                                        struct sdpcm_shared* sh) {
    zx_status_t error;
    struct brcmf_trap_info tr;

    if ((sh->flags & SDPCM_SHARED_TRAP) == 0) {
        brcmf_dbg(INFO, "no trap in firmware\n");
        return ZX_OK;
    }

    error = brcmf_sdiod_ramrw(bus->sdiodev, false, sh->trap_addr, (uint8_t*)&tr,
                              sizeof(struct brcmf_trap_info));
    if (error != ZX_OK) {
        return error;
    }

    seq_printf(seq,
               "dongle trap info: type 0x%x @ epc 0x%08x\n"
               "  cpsr 0x%08x spsr 0x%08x sp 0x%08x\n"
               "  lr   0x%08x pc   0x%08x offset 0x%x\n"
               "  r0   0x%08x r1   0x%08x r2 0x%08x r3 0x%08x\n"
               "  r4   0x%08x r5   0x%08x r6 0x%08x r7 0x%08x\n",
               tr.type, tr.epc, tr.cpsr,
               tr.spsr, tr.r13, tr.r14, tr.pc,
               sh->trap_addr, tr.r0, tr.r1, tr.r2,
               tr.r3, tr.r4, tr.r5, tr.r6,
               tr.r7);

    return ZX_OK;
}

static zx_status_t brcmf_sdio_assert_info(struct seq_file* seq, struct brcmf_sdio* bus,
                                          struct sdpcm_shared* sh) {
    zx_status_t error = ZX_OK;
    char file[80] = "?";
    char expr[80] = "<??""?>";

    if ((sh->flags & SDPCM_SHARED_ASSERT_BUILT) == 0) {
        brcmf_dbg(INFO, "firmware not built with -assert\n");
        return ZX_OK;
    } else if ((sh->flags & SDPCM_SHARED_ASSERT) == 0) {
        brcmf_dbg(INFO, "no assert in dongle\n");
        return ZX_OK;
    }

    sdio_claim_host(bus->sdiodev->func1);
    if (sh->assert_file_addr != 0) {
        error = brcmf_sdiod_ramrw(bus->sdiodev, false, sh->assert_file_addr, (uint8_t*)file, 80);
        if (error != ZX_OK) {
            return error;
        }
    }
    if (sh->assert_exp_addr != 0) {
        error = brcmf_sdiod_ramrw(bus->sdiodev, false, sh->assert_exp_addr, (uint8_t*)expr, 80);
        if (error != ZX_OK) {
            return error;
        }
    }
    sdio_release_host(bus->sdiodev->func1);

    seq_printf(seq, "dongle assert: %s:%d: assert(%s)\n", file, sh->assert_line, expr);
    return ZX_OK;
}

static zx_status_t brcmf_sdio_checkdied(struct brcmf_sdio* bus) {
    zx_status_t error;
    struct sdpcm_shared sh;

    error = brcmf_sdio_readshared(bus, &sh);

    if (error != ZX_OK) {
        return error;
    }

    if ((sh.flags & SDPCM_SHARED_ASSERT_BUILT) == 0) {
        brcmf_dbg(INFO, "firmware not built with -assert\n");
    } else if (sh.flags & SDPCM_SHARED_ASSERT) {
        brcmf_err("assertion in dongle\n");
    }

    if (sh.flags & SDPCM_SHARED_TRAP) {
        brcmf_err("firmware trap in dongle\n");
    }

    return ZX_OK;
}

static zx_status_t brcmf_sdio_died_dump(struct seq_file* seq, struct brcmf_sdio* bus) {
    zx_status_t error = ZX_OK;
    struct sdpcm_shared sh;

    error = brcmf_sdio_readshared(bus, &sh);
    if (error != ZX_OK) {
        goto done;
    }

    error = brcmf_sdio_assert_info(seq, bus, &sh);
    if (error != ZX_OK) {
        goto done;
    }

    error = brcmf_sdio_trap_info(seq, bus, &sh);
    if (error != ZX_OK) {
        goto done;
    }

    error = brcmf_sdio_dump_console(seq, bus, &sh);

done:
    return error;
}

static zx_status_t brcmf_sdio_forensic_read(struct seq_file* seq, void* data) {
    struct brcmf_bus* bus_if = dev_to_bus(seq->private);
    struct brcmf_sdio* bus = bus_if->bus_priv.sdio->bus;

    return brcmf_sdio_died_dump(seq, bus);
}

static zx_status_t brcmf_debugfs_sdio_count_read(struct seq_file* seq, void* data) {
    struct brcmf_bus* bus_if = dev_to_bus(seq->private);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio_count* sdcnt = &sdiodev->bus->sdcnt;

    seq_printf(seq,
               "intrcount:    %u\nlastintrs:    %u\n"
               "pollcnt:      %u\nregfails:     %u\n"
               "tx_sderrs:    %u\nfcqueued:     %u\n"
               "rxrtx:        %u\nrx_toolong:   %u\n"
               "rxc_errors:   %u\nrx_hdrfail:   %u\n"
               "rx_badhdr:    %u\nrx_badseq:    %u\n"
               "fc_rcvd:      %u\nfc_xoff:      %u\n"
               "fc_xon:       %u\nrxglomfail:   %u\n"
               "rxglomframes: %u\nrxglompkts:   %u\n"
               "f2rxhdrs:     %u\nf2rxdata:     %u\n"
               "f2txdata:     %u\nf1regdata:    %u\n"
               "tickcnt:      %u\ntx_ctlerrs:   %lu\n"
               "tx_ctlpkts:   %lu\nrx_ctlerrs:   %lu\n"
               "rx_ctlpkts:   %lu\nrx_readahead: %lu\n",
               sdcnt->intrcount, sdcnt->lastintrs, sdcnt->pollcnt, sdcnt->regfails,
               sdcnt->tx_sderrs, sdcnt->fcqueued, sdcnt->rxrtx, sdcnt->rx_toolong,
               sdcnt->rxc_errors, sdcnt->rx_hdrfail, sdcnt->rx_badhdr, sdcnt->rx_badseq,
               sdcnt->fc_rcvd, sdcnt->fc_xoff, sdcnt->fc_xon, sdcnt->rxglomfail,
               sdcnt->rxglomframes, sdcnt->rxglompkts, sdcnt->f2rxhdrs, sdcnt->f2rxdata,
               sdcnt->f2txdata, sdcnt->f1regdata, sdcnt->tickcnt, sdcnt->tx_ctlerrs,
               sdcnt->tx_ctlpkts, sdcnt->rx_ctlerrs, sdcnt->rx_ctlpkts, sdcnt->rx_readahead_cnt);

    return ZX_OK;
}

static void brcmf_sdio_debugfs_create(struct brcmf_sdio* bus) {
    struct brcmf_pub* drvr = bus->sdiodev->bus_if->drvr;
    zx_handle_t dentry = brcmf_debugfs_get_devdir(drvr);

    if (dentry == ZX_HANDLE_INVALID) {
        return;
    }

    bus->console_interval = BRCMF_CONSOLE;

    brcmf_debugfs_add_entry(drvr, "forensics", brcmf_sdio_forensic_read);
    brcmf_debugfs_add_entry(drvr, "counters", brcmf_debugfs_sdio_count_read);
    brcmf_debugfs_create_u32_file("console_interval", 0644, dentry, &bus->console_interval);
}
#else
static zx_status_t brcmf_sdio_checkdied(struct brcmf_sdio* bus) {
    return ZX_OK;
}

static void brcmf_sdio_debugfs_create(struct brcmf_sdio* bus) {}
#endif /* DEBUG */

static zx_status_t brcmf_sdio_bus_rxctl(struct brcmf_device* dev, unsigned char* msg, uint msglen,
                                        int* rxlen_out) {
    bool timeout;
    uint rxlen = 0;
    bool pending;
    uint8_t* buf;
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;

    brcmf_dbg(TRACE, "Enter\n");
    if (sdiodev->state != BRCMF_SDIOD_DATA) {
        return ZX_ERR_IO;
    }

    /* Wait until control frame is available */
    timeout = (brcmf_sdio_dcmd_resp_wait(bus, &pending) != ZX_OK);

    //spin_lock_bh(&bus->rxctl_lock);
    pthread_mutex_lock(&irq_callback_lock);
    rxlen = bus->rxlen;
    memcpy(msg, bus->rxctl, min(msglen, rxlen));
    bus->rxctl = NULL;
    buf = bus->rxctl_orig;
    bus->rxctl_orig = NULL;
    bus->rxlen = 0;
    //spin_unlock_bh(&bus->rxctl_lock);
    pthread_mutex_unlock(&irq_callback_lock);
    free(buf);

    if (rxlen) {
        brcmf_dbg(CTL, "resumed on rxctl frame, got %d expected %d\n", rxlen, msglen);
    } else if (timeout) {
        brcmf_err("resumed on timeout\n");
        brcmf_sdio_checkdied(bus);
    } else if (pending) {
        brcmf_dbg(CTL, "cancelled\n");
        return ZX_ERR_UNAVAILABLE;
    } else {
        brcmf_dbg(CTL, "resumed for unknown reason?\n");
        brcmf_sdio_checkdied(bus);
    }

    if (rxlen) {
        bus->sdcnt.rx_ctlpkts++;
    } else {
        bus->sdcnt.rx_ctlerrs++;
    }

    if (rxlen) {
        *rxlen_out = rxlen;
        return ZX_OK;
    } else {
        return ZX_ERR_SHOULD_WAIT;
    }
}

#ifdef DEBUG
static bool brcmf_sdio_verifymemory(struct brcmf_sdio_dev* sdiodev, uint32_t ram_addr,
                                    uint8_t* ram_data, uint ram_sz) {
    char* ram_cmp;
    zx_status_t err;
    bool ret = true;
    int address;
    int offset;
    int len;

    /* read back and verify */
    brcmf_dbg(INFO, "Compare RAM dl & ul at 0x%08x; size=%d\n", ram_addr, ram_sz);
    ram_cmp = malloc(MEMBLOCK);
    /* do not proceed while no memory but  */
    if (!ram_cmp) {
        return true;
    }

    address = ram_addr;
    offset = 0;
    while (offset < (int)ram_sz) {
        len = ((offset + MEMBLOCK) < (int)ram_sz) ? MEMBLOCK : ram_sz - offset;
        err = brcmf_sdiod_ramrw(sdiodev, false, address, (uint8_t*)ram_cmp, len);
        if (err != ZX_OK) {
            brcmf_err("error %d on reading %d membytes at 0x%08x\n", err, len, address);
            ret = false;
            break;
        } else if (memcmp(ram_cmp, &ram_data[offset], len)) {
            brcmf_err("Downloaded RAM image is corrupted, block offset is %d, len is %d\n", offset,
                      len);
            ret = false;
            break;
        }
        offset += len;
        address += len;
    }

    free(ram_cmp);

    return ret;
}
#else  /* DEBUG */
static bool brcmf_sdio_verifymemory(struct brcmf_sdio_dev* sdiodev, uint32_t ram_addr,
                                    uint8_t* ram_data, uint ram_sz) {
    return true;
}
#endif /* DEBUG */

static zx_status_t brcmf_sdio_download_code_file(struct brcmf_sdio* bus,
                                                 const struct brcmf_firmware* fw) {
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter\n");

    err = brcmf_sdiod_ramrw(bus->sdiodev, true, bus->ci->rambase, (uint8_t*)fw->data, fw->size);
    if (err != ZX_OK)
        brcmf_err("error %d on writing %d membytes at 0x%08x\n", err, (int)fw->size,
                  bus->ci->rambase);
    else if (!brcmf_sdio_verifymemory(bus->sdiodev, bus->ci->rambase,
                                      (uint8_t*)fw->data, fw->size)) {
        err = ZX_ERR_IO;
    }

    return err;
}

static zx_status_t brcmf_sdio_download_nvram(struct brcmf_sdio* bus, void* vars, uint32_t varsz) {
    int address;
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter\n");

    address = bus->ci->ramsize - varsz + bus->ci->rambase;
    err = brcmf_sdiod_ramrw(bus->sdiodev, true, address, vars, varsz);
    if (err != ZX_OK) {
        brcmf_err("error %d on writing %d nvram bytes at 0x%08x\n", err, varsz, address);
    } else if (!brcmf_sdio_verifymemory(bus->sdiodev, address, vars, varsz)) {
        err = ZX_ERR_IO;
    }

    return err;
}

static zx_status_t brcmf_sdio_download_firmware(struct brcmf_sdio* bus,
                                                const struct brcmf_firmware* fw,
                                                void* nvram, uint32_t nvlen) {
    zx_status_t bcmerror;
    uint32_t rstvec;

    sdio_claim_host(bus->sdiodev->func1);
    brcmf_sdio_clkctl(bus, CLK_AVAIL, false);

    rstvec = get_unaligned_le32(fw->data);
    brcmf_dbg(SDIO, "firmware rstvec: %x\n", rstvec);

    bcmerror = brcmf_sdio_download_code_file(bus, fw);
    if (bcmerror != ZX_OK) {
        brcmf_err("dongle image file download failed\n");
        brcmf_fw_nvram_free(nvram);
        goto err;
    }

    bcmerror = brcmf_sdio_download_nvram(bus, nvram, nvlen);
    brcmf_fw_nvram_free(nvram);
    if (bcmerror != ZX_OK) {
        brcmf_err("dongle nvram file download failed\n");
        goto err;
    }

    /* Take arm out of reset */
    if (!brcmf_chip_set_active(bus->ci, rstvec)) {
        brcmf_err("error getting out of ARM core reset\n");
        goto err;
    }

err:
    brcmf_sdio_clkctl(bus, CLK_SDONLY, false);
    sdio_release_host(bus->sdiodev->func1);
    return bcmerror;
}

static void brcmf_sdio_sr_init(struct brcmf_sdio* bus) {
    zx_status_t err = ZX_OK;
    uint8_t val;

    brcmf_dbg(TRACE, "Enter\n");

    val = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_WAKEUPCTRL, &err);
    if (err != ZX_OK) {
        brcmf_err("error reading SBSDIO_FUNC1_WAKEUPCTRL\n");
        return;
    }

    val |= 1 << SBSDIO_FUNC1_WCTRL_HTWAIT_SHIFT;
    brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_WAKEUPCTRL, val, &err);
    if (err != ZX_OK) {
        brcmf_err("error writing SBSDIO_FUNC1_WAKEUPCTRL\n");
        return;
    }

    /* Add CMD14 Support */
    brcmf_sdiod_func0_wb(bus->sdiodev, SDIO_CCCR_BRCM_CARDCAP,
                         (SDIO_CCCR_BRCM_CARDCAP_CMD14_SUPPORT | SDIO_CCCR_BRCM_CARDCAP_CMD14_EXT),
                         &err);
    if (err != ZX_OK) {
        brcmf_err("error writing SDIO_CCCR_BRCM_CARDCAP\n");
        return;
    }

    brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_FORCE_HT, &err);
    if (err != ZX_OK) {
        brcmf_err("error writing SBSDIO_FUNC1_CHIPCLKCSR\n");
        return;
    }

    /* set flag */
    bus->sr_enabled = true;
    brcmf_dbg(INFO, "SR enabled\n");
}

/* enable KSO bit */
static zx_status_t brcmf_sdio_kso_init(struct brcmf_sdio* bus) {
    struct brcmf_core* core = bus->sdio_core;
    uint8_t val;
    zx_status_t err = ZX_OK;

    brcmf_dbg(TRACE, "Enter\n");

    /* KSO bit added in SDIO core rev 12 */
    if (core->rev < 12) {
        return ZX_OK;
    }

    val = brcmf_sdiod_readb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, &err);
    if (err != ZX_OK) {
        brcmf_err("error reading SBSDIO_FUNC1_SLEEPCSR\n");
        return err;
    }

    if (!(val & SBSDIO_FUNC1_SLEEPCSR_KSO_MASK)) {
        val |= (SBSDIO_FUNC1_SLEEPCSR_KSO_EN << SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT);
        brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, val, &err);
        if (err != ZX_OK) {
            brcmf_err("error writing SBSDIO_FUNC1_SLEEPCSR\n");
            return err;
        }
    }

    return ZX_OK;
}

static zx_status_t brcmf_sdio_bus_preinit(struct brcmf_device* dev) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;
    struct brcmf_core* core = bus->sdio_core;
    uint32_t value;
    zx_status_t err;

    /* the commands below use the terms tx and rx from
     * a device perspective, ie. bus:txglom affects the
     * bus transfers from device to host.
     */
    if (core->rev < 12) {
        /* for sdio core rev < 12, disable txgloming */
        value = 0;
        err = brcmf_iovar_data_set(dev, "bus:txglom", &value, sizeof(uint32_t));
    } else {
        /* otherwise, set txglomalign */
        value = sdiodev->settings->bus.sdio.sd_sgentry_align;
        /* SDIO ADMA requires at least 32 bit alignment */
        value = max(value, ALIGNMENT);
        err = brcmf_iovar_data_set(dev, "bus:txglomalign", &value, sizeof(uint32_t));
    }

    if (err != ZX_OK) {
        goto done;
    }

    bus->tx_hdrlen = SDPCM_HWHDR_LEN + SDPCM_SWHDR_LEN;
    brcmf_bus_add_txhdrlen(&bus->sdiodev->dev, bus->tx_hdrlen);

done:
    return err;
}

static size_t brcmf_sdio_bus_get_ramsize(struct brcmf_device* dev) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;

    return bus->ci->ramsize - bus->ci->srsize;
}

static zx_status_t brcmf_sdio_bus_get_memdump(struct brcmf_device* dev, void* data,
                                              size_t mem_size) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;
    zx_status_t err;
    int address;
    size_t offset; // clang needs this unsigned
    int len;

    brcmf_dbg(INFO, "dump at 0x%08x: size=%zu\n", bus->ci->rambase, mem_size);

    address = bus->ci->rambase;
    offset = 0;
    err = ZX_OK;
    sdio_claim_host(sdiodev->func1);
    while (offset < mem_size) {
        len = ((offset + MEMBLOCK) < mem_size) ? MEMBLOCK : mem_size - offset;
        err = brcmf_sdiod_ramrw(sdiodev, false, address, data, len);
        if (err != ZX_OK) {
            brcmf_err("error %d on reading %d membytes at 0x%08x\n", err, len, address);
            goto done;
        }
        data += len;
        offset += len;
        address += len;
    }

done:
    sdio_release_host(sdiodev->func1);
    return err;
}

void brcmf_sdio_trigger_dpc(struct brcmf_sdio* bus) {
    if (!bus->dpc_triggered) {
        bus->dpc_triggered = true;
        workqueue_schedule(bus->brcmf_wq, &bus->datawork);
    }
}

void brcmf_sdio_isr(struct brcmf_sdio* bus) {
    brcmf_dbg(TRACE, "Enter\n");

    if (!bus) {
        brcmf_err("bus is null pointer, exiting\n");
        return;
    }

    /* Count the interrupt call */
    bus->sdcnt.intrcount++;
    if (brcmf_sdio_intr_rstatus(bus) != ZX_OK) {
        brcmf_err("failed backplane access\n");
    }

    /* Disable additional interrupts (is this needed now)? */
    if (!bus->intr) {
        brcmf_err("isr w/o interrupt configured!\n");
    }

    bus->dpc_triggered = true;
    workqueue_schedule(bus->brcmf_wq, &bus->datawork);
}

static void brcmf_sdio_bus_watchdog(struct brcmf_sdio* bus) {
    brcmf_dbg(TIMER, "Enter\n");

    /* Poll period: check device if appropriate. */
    if (!bus->sr_enabled && bus->poll && (++bus->polltick >= bus->pollrate)) {
        uint32_t intstatus = 0;

        /* Reset poll tick */
        bus->polltick = 0;

        /* Check device if no interrupts */
        if (!bus->intr || (bus->sdcnt.intrcount == bus->sdcnt.lastintrs)) {
            if (!bus->dpc_triggered) {
                uint8_t devpend;

                sdio_claim_host(bus->sdiodev->func1);
                devpend = brcmf_sdiod_func0_rb(bus->sdiodev, SDIO_CCCR_INTx, NULL);
                sdio_release_host(bus->sdiodev->func1);
                intstatus = devpend & (INTR_STATUS_FUNC1 | INTR_STATUS_FUNC2);
            }

            /* If there is something, make like the ISR and
                     schedule the DPC. */
            if (intstatus) {
                bus->sdcnt.pollcnt++;
                atomic_store(&bus->ipend, 1);

                bus->dpc_triggered = true;
                workqueue_schedule(bus->brcmf_wq, &bus->datawork);
            }
        }

        /* Update interrupt tracking */
        bus->sdcnt.lastintrs = bus->sdcnt.intrcount;
    }
#ifdef DEBUG
    /* Poll for console output periodically */
    if (bus->sdiodev->state == BRCMF_SDIOD_DATA && BRCMF_FWCON_ON() && bus->console_interval != 0) {
        bus->console.count += BRCMF_WD_POLL_MSEC;
        if (bus->console.count >= bus->console_interval) {
            bus->console.count -= bus->console_interval;
            sdio_claim_host(bus->sdiodev->func1);
            /* Make sure backplane clock is on */
            brcmf_sdio_bus_sleep(bus, false, false);
            if (brcmf_sdio_readconsole(bus) != ZX_OK) { /* stop on error */
                bus->console_interval = 0;
            }
            sdio_release_host(bus->sdiodev->func1);
        }
    }
#endif /* DEBUG */

    /* On idle timeout clear activity flag and/or turn off clock */
    if (!bus->dpc_triggered) {
        rmb();
        if ((!bus->dpc_running) && (bus->idletime > 0) && (bus->clkstate == CLK_AVAIL)) {
            bus->idlecount++;
            if (bus->idlecount > bus->idletime) {
                brcmf_dbg(SDIO, "idle\n");
                sdio_claim_host(bus->sdiodev->func1);
                brcmf_sdio_wd_timer(bus, false);
                bus->idlecount = 0;
                brcmf_sdio_bus_sleep(bus, true, false);
                sdio_release_host(bus->sdiodev->func1);
            }
        } else {
            bus->idlecount = 0;
        }
    } else {
        bus->idlecount = 0;
    }
}

static void brcmf_sdio_dataworker(struct work_struct* work) {
    struct brcmf_sdio* bus = containerof(work, struct brcmf_sdio, datawork);

    bus->dpc_running = true;
    wmb();
    while (*(volatile bool*)&(bus->dpc_triggered)) {
        bus->dpc_triggered = false;
        brcmf_sdio_dpc(bus);
        bus->idlecount = 0;
    }
    bus->dpc_running = false;
    if (brcmf_sdiod_freezing(bus->sdiodev)) {
        brcmf_sdiod_change_state(bus->sdiodev, BRCMF_SDIOD_DOWN);
        brcmf_sdiod_try_freeze(bus->sdiodev);
        brcmf_sdiod_change_state(bus->sdiodev, BRCMF_SDIOD_DATA);
    }
}

static void brcmf_sdio_drivestrengthinit(struct brcmf_sdio_dev* sdiodev, struct brcmf_chip* ci,
                                         uint32_t drivestrength) {
    const struct sdiod_drive_str* str_tab = NULL;
    uint32_t str_mask;
    uint32_t str_shift;
    uint32_t i;
    uint32_t drivestrength_sel = 0;
    uint32_t cc_data_temp;
    uint32_t addr;

    if (!(ci->cc_caps & CC_CAP_PMU)) {
        return;
    }

    switch (SDIOD_DRVSTR_KEY(ci->chip, ci->pmurev)) {
    case SDIOD_DRVSTR_KEY(BRCM_CC_4330_CHIP_ID, 12):
        str_tab = sdiod_drvstr_tab1_1v8;
        str_mask = 0x00003800;
        str_shift = 11;
        break;
    case SDIOD_DRVSTR_KEY(BRCM_CC_4334_CHIP_ID, 17):
        str_tab = sdiod_drvstr_tab6_1v8;
        str_mask = 0x00001800;
        str_shift = 11;
        break;
    case SDIOD_DRVSTR_KEY(BRCM_CC_43143_CHIP_ID, 17):
        /* note: 43143 does not support tristate */
        i = ARRAY_SIZE(sdiod_drvstr_tab2_3v3) - 1;
        if (drivestrength >= sdiod_drvstr_tab2_3v3[i].strength) {
            str_tab = sdiod_drvstr_tab2_3v3;
            str_mask = 0x00000007;
            str_shift = 0;
        } else
            brcmf_err("Invalid SDIO Drive strength for chip %s, strength=%d\n", ci->name,
                      drivestrength);
        break;
    case SDIOD_DRVSTR_KEY(BRCM_CC_43362_CHIP_ID, 13):
        str_tab = sdiod_drive_strength_tab5_1v8;
        str_mask = 0x00003800;
        str_shift = 11;
        break;
    default:
        brcmf_dbg(INFO, "No SDIO driver strength init needed for chip %s rev %d pmurev %d\n",
                  ci->name, ci->chiprev, ci->pmurev);
        break;
    }

    if (str_tab != NULL) {
        struct brcmf_core* pmu = brcmf_chip_get_pmu(ci);

        for (i = 0; str_tab[i].strength != 0; i++) {
            if (drivestrength >= str_tab[i].strength) {
                drivestrength_sel = str_tab[i].sel;
                break;
            }
        }
        addr = CORE_CC_REG(pmu->base, chipcontrol_addr);
        brcmf_sdiod_writel(sdiodev, addr, 1, NULL);
        cc_data_temp = brcmf_sdiod_readl(sdiodev, addr, NULL);
        cc_data_temp &= ~str_mask;
        drivestrength_sel <<= str_shift;
        cc_data_temp |= drivestrength_sel;
        brcmf_sdiod_writel(sdiodev, addr, cc_data_temp, NULL);

        brcmf_dbg(INFO, "SDIO: %d mA (req=%d mA) drive strength selected, set to 0x%08x\n",
                  str_tab[i].strength, drivestrength, cc_data_temp);
    }
}

static zx_status_t brcmf_sdio_buscoreprep(void* ctx) {
    struct brcmf_sdio_dev* sdiodev = ctx;
    zx_status_t err = ZX_OK;
    uint8_t clkval, clkset;

    /* Try forcing SDIO core to do ALPAvail request only */
    clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ;
    brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
    if (err != ZX_OK) {
        brcmf_err("error writing for HT off\n");
        return err;
    }

    /* If register supported, wait for ALPAvail and then force ALP */
    /* This may take up to 15 milliseconds */
    clkval = brcmf_sdiod_readb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, NULL);

    if ((clkval & ~SBSDIO_AVBITS) != clkset) {
        brcmf_err("ChipClkCSR access: wrote 0x%02x read 0x%02x\n", clkset, clkval);
        return ZX_ERR_IO_REFUSED;
    }

    SPINWAIT(((clkval = brcmf_sdiod_readb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, NULL)),
              !SBSDIO_ALPAV(clkval)),
             PMU_MAX_TRANSITION_DLY_USEC);

    if (!SBSDIO_ALPAV(clkval)) {
        brcmf_err("timeout on ALPAV wait, clkval 0x%02x\n", clkval);
        return ZX_ERR_SHOULD_WAIT;
    }

    clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP;
    brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
    usleep(65);

    /* Also, disable the extra SDIO pull-ups */
    brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_SDIOPULLUP, 0, NULL);

    return ZX_OK;
}

static void brcmf_sdio_buscore_activate(void* ctx, struct brcmf_chip* chip, uint32_t rstvec) {
    struct brcmf_sdio_dev* sdiodev = ctx;
    struct brcmf_core* core = sdiodev->bus->sdio_core;
    uint32_t reg_addr;

    /* clear all interrupts */
    reg_addr = core->base + SD_REG(intstatus);
    brcmf_sdiod_writel(sdiodev, reg_addr, 0xFFFFFFFF, NULL);

    if (rstvec) { /* Write reset vector to address 0 */
        brcmf_sdiod_ramrw(sdiodev, true, 0, (void*)&rstvec, sizeof(rstvec));
    }
}

static uint32_t brcmf_sdio_buscore_read32(void* ctx, uint32_t addr) {
    struct brcmf_sdio_dev* sdiodev = ctx;
    uint32_t val, rev;

    val = brcmf_sdiod_readl(sdiodev, addr, NULL);

    /*
     * this is a bit of special handling if reading the chipcommon chipid
     * register. The 4339 is a next-gen of the 4335. It uses the same
     * SDIO device id as 4335 and the chipid register returns 4335 as well.
     * It can be identified as 4339 by looking at the chip revision. It
     * is corrected here so the chip.c module has the right info.
     */
    if (addr == CORE_CC_REG(SI_ENUM_BASE, chipid) &&
            (sdiodev->product_id == SDIO_DEVICE_ID_BROADCOM_4339 ||
             sdiodev->product_id == SDIO_DEVICE_ID_BROADCOM_4335_4339)) {
        rev = (val & CID_REV_MASK) >> CID_REV_SHIFT;
        if (rev >= 2) {
            val &= ~CID_ID_MASK;
            val |= BRCM_CC_4339_CHIP_ID;
        }
    }

    return val;
}

static void brcmf_sdio_buscore_write32(void* ctx, uint32_t addr, uint32_t val) {
    struct brcmf_sdio_dev* sdiodev = ctx;

    brcmf_sdiod_writel(sdiodev, addr, val, NULL);
}

static const struct brcmf_buscore_ops brcmf_sdio_buscore_ops = {
    .prepare = brcmf_sdio_buscoreprep,
    .activate = brcmf_sdio_buscore_activate,
    .read32 = brcmf_sdio_buscore_read32,
    .write32 = brcmf_sdio_buscore_write32,
};

static bool brcmf_sdio_probe_attach(struct brcmf_sdio* bus) {
    struct brcmf_sdio_dev* sdiodev;
    uint8_t clkctl = 0;
    zx_status_t err = ZX_OK;
    int reg_addr;
    uint32_t reg_val;
    uint32_t drivestrength;

    sdiodev = bus->sdiodev;
    sdio_claim_host(sdiodev->func1);

    zxlogf(INFO, "brcmfmac: F1 signature read @0x18000000=0x%4x\n",
             brcmf_sdiod_readl(sdiodev, SI_ENUM_BASE, NULL));

    /*
     * Force PLL off until brcmf_chip_attach()
     * programs PLL control regs
     */

    brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, BRCMF_INIT_CLKCTL1, &err);
    if (err == ZX_OK) {
        clkctl = brcmf_sdiod_readb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
    }

    if (err != ZX_OK || ((clkctl & ~SBSDIO_AVBITS) != BRCMF_INIT_CLKCTL1)) {
        brcmf_err("ChipClkCSR access: err %s wrote 0x%02x read 0x%02x\n", zx_status_get_string(err),
                  BRCMF_INIT_CLKCTL1, clkctl);
        goto fail;
    }

    err = brcmf_chip_attach(sdiodev, &brcmf_sdio_buscore_ops, &bus->ci);
    if (err != ZX_OK) {
        brcmf_err("brcmf_chip_attach failed!\n");
        bus->ci = NULL;
        goto fail;
    }

    /* Pick up the SDIO core info struct from chip.c */
    bus->sdio_core = brcmf_chip_get_core(bus->ci, CHIPSET_SDIO_DEV_CORE);
    if (!bus->sdio_core) {
        goto fail;
    }

    /* Pick up the CHIPCOMMON core info struct, for bulk IO in bcmsdh.c */
    sdiodev->cc_core = brcmf_chip_get_core(bus->ci, CHIPSET_CHIPCOMMON_CORE);
    if (!sdiodev->cc_core) {
        goto fail;
    }

    sdiodev->settings =
        brcmf_get_module_param(&sdiodev->dev, BRCMF_BUSTYPE_SDIO, bus->ci->chip, bus->ci->chiprev);
    if (!sdiodev->settings) {
        brcmf_err("Failed to get device parameters\n");
        goto fail;
    }
    /* platform specific configuration:
     *   alignments must be at least 4 bytes for ADMA
     */
    bus->head_align = ALIGNMENT;
    bus->sgentry_align = ALIGNMENT;
    if (sdiodev->settings->bus.sdio.sd_head_align > ALIGNMENT) {
        bus->head_align = sdiodev->settings->bus.sdio.sd_head_align;
    }
    if (sdiodev->settings->bus.sdio.sd_sgentry_align > ALIGNMENT) {
        bus->sgentry_align = sdiodev->settings->bus.sdio.sd_sgentry_align;
    }

#ifdef CONFIG_PM_SLEEP
    /* wowl can be supported when KEEP_POWER is true and (WAKE_SDIO_IRQ
     * is true or when platform data OOB irq is true).
     */
    if ((sdio_get_host_pm_caps(sdiodev->func1) & MMC_PM_KEEP_POWER) &&
            ((sdio_get_host_pm_caps(sdiodev->func1) & MMC_PM_WAKE_SDIO_IRQ) ||
             (sdiodev->settings->bus.sdio.oob_irq_supported))) {
        sdiodev->bus_if->wowl_supported = true;
    }
#endif

    if (brcmf_sdio_kso_init(bus)) {
        brcmf_err("error enabling KSO\n");
        goto fail;
    }

    if (sdiodev->settings->bus.sdio.drive_strength) {
        drivestrength = sdiodev->settings->bus.sdio.drive_strength;
    } else {
        drivestrength = DEFAULT_SDIO_DRIVE_STRENGTH;
    }
    brcmf_sdio_drivestrengthinit(sdiodev, bus->ci, drivestrength);

    /* Set card control so an SDIO card reset does a WLAN backplane reset */
    reg_val = brcmf_sdiod_func0_rb(sdiodev, SDIO_CCCR_BRCM_CARDCTRL, &err);
    if (err != ZX_OK) {
        goto fail;
    }

    reg_val |= SDIO_CCCR_BRCM_CARDCTRL_WLANRESET;

    brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_BRCM_CARDCTRL, reg_val, &err);
    if (err != ZX_OK) {
        goto fail;
    }

    /* set PMUControl so a backplane reset does PMU state reload */
    reg_addr = CORE_CC_REG(brcmf_chip_get_pmu(bus->ci)->base, pmucontrol);
    reg_val = brcmf_sdiod_readl(sdiodev, reg_addr, &err);
    if (err != ZX_OK) {
        goto fail;
    }

    reg_val |= (BC_CORE_POWER_CONTROL_RELOAD << BC_CORE_POWER_CONTROL_SHIFT);

    brcmf_sdiod_writel(sdiodev, reg_addr, reg_val, &err);
    if (err != ZX_OK) {
        goto fail;
    }

    sdio_release_host(sdiodev->func1);

    brcmu_pktq_init(&bus->txq, (PRIOMASK + 1), TXQLEN);

    /* allocate header buffer */
    bus->hdrbuf = calloc(1, MAX_HDR_READ + bus->head_align);
    if (!bus->hdrbuf) {
        return false;
    }
    /* Locate an appropriately-aligned portion of hdrbuf */
    bus->rxhdr = (uint8_t*)roundup((unsigned long)&bus->hdrbuf[0], bus->head_align);

    /* Set the poll and/or interrupt flags */
    bus->intr = true;
    bus->poll = false;
    if (bus->poll) {
        bus->pollrate = 1;
    }

    return true;

fail:
    sdio_release_host(sdiodev->func1);
    return false;
}

static zx_status_t brcmf_sdio_watchdog_thread(void* data) {
    struct brcmf_sdio* bus = (struct brcmf_sdio*)data;
    int wait;

    allow_signal(SIGTERM);
    /* Run until signal received */
    brcmf_sdiod_freezer_count(bus->sdiodev);
    while (1) {
        if (kthread_should_stop()) {
            break;
        }
        brcmf_sdiod_freezer_uncount(bus->sdiodev);
        wait = sync_completion_wait(&bus->watchdog_wait, ZX_TIME_INFINITE);
        brcmf_sdiod_freezer_count(bus->sdiodev);
        brcmf_sdiod_try_freeze(bus->sdiodev);
        if (!wait) {
            brcmf_sdio_bus_watchdog(bus);
            /* Count the tick for reference */
            bus->sdcnt.tickcnt++;
            sync_completion_reset(&bus->watchdog_wait);
        } else {
            break;
        }
    }
    return ZX_OK;
}

static void brcmf_sdio_watchdog(void* data) {
    pthread_mutex_lock(&irq_callback_lock);
    struct brcmf_sdio* bus = data;

    if (bus->watchdog_tsk) {
        sync_completion_signal(&bus->watchdog_wait);
        /* Reschedule the watchdog */
        if (bus->wd_active) {
            brcmf_timer_set(&bus->timer, ZX_MSEC(BRCMF_WD_POLL_MSEC));
        }
    }
    pthread_mutex_unlock(&irq_callback_lock);
}

static zx_status_t brcmf_sdio_get_fwname(struct brcmf_device* dev, uint32_t chip, uint32_t chiprev,
                                         uint8_t* fw_name) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    int ret = ZX_OK;

    if (sdiodev->fw_name[0] != '\0') {
        strlcpy((char*)fw_name, sdiodev->fw_name, BRCMF_FW_NAME_LEN);
    } else {
        ret = brcmf_fw_map_chip_to_name(chip, chiprev, brcmf_sdio_fwnames,
                                        ARRAY_SIZE(brcmf_sdio_fwnames), (char*)fw_name, NULL);
    }
    return ret;
}

static const struct brcmf_bus_ops brcmf_sdio_bus_ops = {
    .stop = brcmf_sdio_bus_stop,
    .preinit = brcmf_sdio_bus_preinit,
    .txdata = brcmf_sdio_bus_txdata,
    .txctl = brcmf_sdio_bus_txctl,
    .rxctl = brcmf_sdio_bus_rxctl,
    .gettxq = brcmf_sdio_bus_gettxq,
    .wowl_config = brcmf_sdio_wowl_config,
    .get_ramsize = brcmf_sdio_bus_get_ramsize,
    .get_memdump = brcmf_sdio_bus_get_memdump,
    .get_fwname = brcmf_sdio_get_fwname,
};

static void brcmf_sdio_firmware_callback(struct brcmf_device* dev, zx_status_t err,
                                         const struct brcmf_firmware* code,
                                         void* nvram, uint32_t nvram_len) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
    struct brcmf_sdio* bus = sdiodev->bus;
    struct brcmf_sdio_dev* sdiod = bus->sdiodev;
    struct brcmf_core* core = bus->sdio_core;
    uint8_t saveclk;

    brcmf_dbg(TRACE, "Enter: dev=%s, err=%d\n", device_get_name(dev->zxdev), err);

    if (err != ZX_OK) {
        goto fail;
    }

    if (!bus_if->drvr) {
        return;
    }

    /* try to download image and nvram to the dongle */
    bus->alp_only = true;
    err = brcmf_sdio_download_firmware(bus, code, nvram, nvram_len);
    if (err != ZX_OK) {
        goto fail;
    }
    bus->alp_only = false;

    /* Start the watchdog timer */
    bus->sdcnt.tickcnt = 0;
    brcmf_sdio_wd_timer(bus, true);

    sdio_claim_host(sdiodev->func1);

    /* Make sure backplane clock is on, needed to generate F2 interrupt */
    brcmf_sdio_clkctl(bus, CLK_AVAIL, false);
    if (bus->clkstate != CLK_AVAIL) {
        goto release;
    }

    /* Force clocks on backplane to be sure F2 interrupt propagates */
    saveclk = brcmf_sdiod_readb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
    if (err == ZX_OK) {
        brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, (saveclk | SBSDIO_FORCE_HT), &err);
    }
    if (err != ZX_OK) {
        brcmf_err("Failed to force clock for F2: err %s\n", zx_status_get_string(err));
        goto release;
    }

    /* Enable function 2 (frame transfers) */
    brcmf_sdiod_writel(sdiod, core->base + SD_REG(tosbmailboxdata),
                       SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT, NULL);

    err = sdio_enable_fn(sdiodev->sdio_proto, SDIO_FN_2);

    brcmf_dbg(INFO, "enable F2: err=%d\n", err);

    /* If F2 successfully enabled, set core and enable interrupts */
    if (err == ZX_OK) {
        /* Set up the interrupt mask and enable interrupts */
        bus->hostintmask = HOSTINTMASK;
        brcmf_sdiod_writel(sdiod, core->base + SD_REG(hostintmask), bus->hostintmask, NULL);

        brcmf_sdiod_writeb(sdiodev, SBSDIO_WATERMARK, 8, &err);
    } else {
        /* Disable F2 again */
        sdio_disable_fn(sdiodev->sdio_proto, SDIO_FN_2);
        goto release;
    }

    if (brcmf_chip_sr_capable(bus->ci)) {
        brcmf_sdio_sr_init(bus);
    } else {
        /* Restore previous clock setting */
        brcmf_sdiod_writeb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, saveclk, &err);
    }

    if (err == ZX_OK) {
        /* Allow full data communication using DPC from now on. */
        brcmf_sdiod_change_state(bus->sdiodev, BRCMF_SDIOD_DATA);

        err = brcmf_sdiod_intr_register(sdiodev);
        if (err != ZX_OK) {
            brcmf_err("intr register failed:%d\n", err);
        }
    }

    /* If we didn't come up, turn off backplane clock */
    if (err != ZX_OK) {
        brcmf_sdio_clkctl(bus, CLK_NONE, false);
    }

    sdio_release_host(sdiodev->func1);

    err = brcmf_bus_started(dev);
    if (err != ZX_OK) {
        brcmf_err("dongle is not responding\n");
        goto fail;
    }
    return;

release:
    sdio_release_host(sdiodev->func1);
fail:
    brcmf_dbg(TRACE, "failed: dev=%s, err=%d\n", device_get_name(dev->zxdev), err);
    // TODO(cphoenix): Do the right calls here to release the driver
    brcmf_err("* * Used to call device_release_driver(&sdiodev->func2->dev);");
    brcmf_err("* * Used to call device_release_driver(dev);");

}

struct brcmf_sdio* brcmf_sdio_probe(struct brcmf_sdio_dev* sdiodev) {
    zx_status_t ret;
    struct brcmf_sdio* bus;
    struct workqueue_struct* wq;

    brcmf_dbg(TRACE, "Enter\n");

    /* Allocate private bus interface state */
    bus = calloc(1, sizeof(struct brcmf_sdio));
    if (!bus) {
        goto fail;
    }

    bus->sdiodev = sdiodev;
    sdiodev->bus = bus;
    brcmf_netbuf_list_init(&bus->glom);
    bus->txbound = BRCMF_TXBOUND;
    bus->rxbound = BRCMF_RXBOUND;
    bus->txminmax = BRCMF_TXMINMAX;
    bus->tx_seq = SDPCM_SEQ_WRAP - 1;

    /* single-threaded workqueue */
    char name[WORKQUEUE_NAME_MAXLEN];
    static int queue_uniquify = 0;
    snprintf(name, WORKQUEUE_NAME_MAXLEN, "brcmf_wq/%s%d",
             device_get_name(sdiodev->dev.zxdev), queue_uniquify++);
    wq = workqueue_create(name);
    if (!wq) {
        brcmf_err("insufficient memory to create txworkqueue\n");
        goto fail;
    }
    brcmf_sdiod_freezer_count(sdiodev);
    workqueue_init_work(&bus->datawork, brcmf_sdio_dataworker);
    bus->brcmf_wq = wq;

    /* attempt to attach to the dongle */
    if (!(brcmf_sdio_probe_attach(bus))) {
        brcmf_err("brcmf_sdio_probe_attach failed\n");
        goto fail;
    }

    //spin_lock_init(&bus->rxctl_lock);
    //spin_lock_init(&bus->txq_lock);
    bus->ctrl_wait = SYNC_COMPLETION_INIT;
    bus->dcmd_resp_wait = SYNC_COMPLETION_INIT;

    /* Set up the watchdog timer */
    brcmf_timer_init(&bus->timer, brcmf_sdio_watchdog);
    /* Initialize watchdog thread */
    bus->watchdog_wait = SYNC_COMPLETION_INIT;
    // TODO(cphoenix): Hack to make it compile - will be fixed when we support SDIO.
    brcmf_err("* * Need to do ret = kthread_run(brcmf_sdio_watchdog_thread, ...");
    (void)brcmf_sdio_watchdog_thread;
//    ret = kthread_run(brcmf_sdio_watchdog_thread, bus, "brcmf_wdog/%s",
//                      device_get_name(sdiodev->dev.zxdev), &bus->watchdog_tsk);
//    if (ret != ZX_OK) {
//        pr_warn("brcmf_watchdog thread failed to start\n");
//        bus->watchdog_tsk = NULL;
//    }
    /* Initialize DPC thread */
    bus->dpc_triggered = false;
    bus->dpc_running = false;

    /* Assign bus interface call back */
    bus->sdiodev->bus_if->dev = &bus->sdiodev->dev;
    bus->sdiodev->bus_if->ops = &brcmf_sdio_bus_ops;
    bus->sdiodev->bus_if->chip = bus->ci->chip;
    bus->sdiodev->bus_if->chiprev = bus->ci->chiprev;

    /* default sdio bus header length for tx packet */
    bus->tx_hdrlen = SDPCM_HWHDR_LEN + SDPCM_SWHDR_LEN;

    /* Attach to the common layer, reserve hdr space */
    ret = brcmf_attach(&bus->sdiodev->dev, bus->sdiodev->settings);
    if (ret != ZX_OK) {
        brcmf_err("brcmf_attach failed\n");
        goto fail;
    }

    /* Query the F2 block size, set roundup accordingly */
    sdio_get_block_size(sdiodev->sdio_proto, SDIO_FN_2, &bus->blocksize);
    bus->roundup = min(max_roundup, bus->blocksize);

    /* Allocate buffers */
    if (bus->sdiodev->bus_if->maxctl) {
        bus->sdiodev->bus_if->maxctl += bus->roundup;
        bus->rxblen =
            roundup((bus->sdiodev->bus_if->maxctl + SDPCM_HDRLEN), ALIGNMENT) + bus->head_align;
        bus->rxbuf = malloc(bus->rxblen);
        if (!(bus->rxbuf)) {
            brcmf_err("rxbuf allocation failed\n");
            goto fail;
        }
    }

    sdio_claim_host(bus->sdiodev->func1);

    /* Disable F2 to clear any intermediate frame state on the dongle */
    sdio_disable_fn(bus->sdiodev->sdio_proto, SDIO_FN_2);

    bus->rxflow = false;

    /* Done with backplane-dependent accesses, can drop clock... */
    brcmf_sdiod_writeb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);

    sdio_release_host(bus->sdiodev->func1);

    /* ...and initialize clock/power states */
    bus->clkstate = CLK_SDONLY;
    bus->idletime = BRCMF_IDLE_INTERVAL;
    bus->idleclock = BRCMF_IDLE_ACTIVE;

    /* SR state */
    bus->sr_enabled = false;

    brcmf_sdio_debugfs_create(bus);
    brcmf_dbg(INFO, "completed!!\n");

    ret = brcmf_fw_map_chip_to_name(bus->ci->chip, bus->ci->chiprev, brcmf_sdio_fwnames,
                                    ARRAY_SIZE(brcmf_sdio_fwnames), sdiodev->fw_name,
                                    sdiodev->nvram_name);
    if (ret != ZX_OK) {
        goto fail;
    }

    ret = brcmf_fw_get_firmwares(&sdiodev->dev, BRCMF_FW_REQUEST_NVRAM, sdiodev->fw_name,
                                 sdiodev->nvram_name, brcmf_sdio_firmware_callback);
    if (ret != ZX_OK) {
        brcmf_err("async firmware request failed: %d\n", ret);
        goto fail;
    }

    return bus;

fail:
    brcmf_sdio_remove(bus);
    return NULL;
}

/* Detach and free everything */
void brcmf_sdio_remove(struct brcmf_sdio* bus) {
    brcmf_dbg(TRACE, "Enter\n");

    if (bus) {
        /* De-register interrupt handler */
        brcmf_sdiod_intr_unregister(bus->sdiodev);

        brcmf_detach(&bus->sdiodev->dev);

        workqueue_cancel_work(&bus->datawork);
        if (bus->brcmf_wq) {
            workqueue_destroy(bus->brcmf_wq);
        }

        if (bus->ci) {
            if (bus->sdiodev->state != BRCMF_SDIOD_NOMEDIUM) {
                sdio_claim_host(bus->sdiodev->func1);
                brcmf_sdio_wd_timer(bus, false);
                brcmf_sdio_clkctl(bus, CLK_AVAIL, false);
                /* Leave the device in state where it is
                 * 'passive'. This is done by resetting all
                 * necessary cores.
                 */
                msleep(20);
                brcmf_chip_set_passive(bus->ci);
                brcmf_sdio_clkctl(bus, CLK_NONE, false);
                sdio_release_host(bus->sdiodev->func1);
            }
            brcmf_chip_detach(bus->ci);
        }
        if (bus->sdiodev->settings) {
            brcmf_release_module_param(bus->sdiodev->settings);
        }

        free(bus->rxbuf);
        free(bus->hdrbuf);
        free(bus);
    }

    brcmf_dbg(TRACE, "Disconnected\n");
}

void brcmf_sdio_wd_timer(struct brcmf_sdio* bus, bool active) {
    /* Totally stop the timer */
    if (!active && bus->wd_active) {
        brcmf_timer_stop(&bus->timer);
        bus->wd_active = false;
        return;
    }

    /* don't start the wd until fw is loaded */
    if (bus->sdiodev->state != BRCMF_SDIOD_DATA) {
        return;
    }

    if (active) {
        if (!bus->wd_active) {
            /* Create timer again when watchdog period is
               dynamically changed or in the first instance
             */
            brcmf_timer_set(&bus->timer, ZX_MSEC(BRCMF_WD_POLL_MSEC));
            bus->wd_active = true;
        } else {
            /* Re arm the timer, at last watchdog period */
            brcmf_timer_set(&bus->timer, ZX_MSEC(BRCMF_WD_POLL_MSEC));
        }
    }
}

zx_status_t brcmf_sdio_sleep(struct brcmf_sdio* bus, bool sleep) {
    zx_status_t ret;

    sdio_claim_host(bus->sdiodev->func1);
    ret = brcmf_sdio_bus_sleep(bus, sleep, false);
    sdio_release_host(bus->sdiodev->func1);

    return ret;
}
