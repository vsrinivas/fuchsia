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

#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/trace/event.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/align.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <atomic>
#include <limits>

#include <wifi/wifi-config.h>
#include <wlan/drivers/components/frame.h>

#ifndef _ALL_SOURCE
#define _ALL_SOURCE
#endif
#include <threads.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bcdc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_utils.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_wifi.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chip.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/defs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/device_inspect.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/linuxisms.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/soc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/timer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/workqueue.h"

#define DCMD_RESP_TIMEOUT_MSEC (2500)

#ifdef BRCMF_CONSOLE_LOG

#define BRCMF_TRAP_INFO_SIZE 80

#define CBUF_LEN (128)

/* Device console log buffer state */
#define CONSOLE_BUFFER_MAX 2024

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

#endif  // BRCMF_CONSOLE_LOG
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipcommon.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

#define TXQLEN 2048         /* bulk tx queue length */
#define RXDEPTH 2048        /* Amount of RX space to request */
#define TXHI (TXQLEN - 256) /* turn on flow control above TXHI */
#define TXLOW (TXHI - 256)  /* turn off flow control below TXLOW */
#define PRIOMASK 7

#define TXRETRIES 2 /* # of retries for tx frames */

/* Default for max rx frames in
   one scheduling */
#define BRCMF_RXBOUND 50

/* Default for max tx frames in one scheduling */
#define BRCMF_TXBOUND 40

#define BRCMF_TXMINMAX 1 /* Max tx frames if rx still pending */

/* Block size used for downloading
   of dongle image */
#define MEMBLOCK 2048
/* Must be large enough to hold
   biggest possible glom */
#define MAX_DATA_BUF (32 * 1024)

#define BRCMF_FIRSTREAD (1 << 6)

#define BRCMF_CONSOLE_INTERVAL 100 /* watchdog interval to poll console */

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

#define BRCMF_IDLE_ACTIVE 0 /* Do not request any SD clock change when idle */
#define BRCMF_IDLE_INTERVAL 1

#define KSO_WAIT_US 50
#define MAX_KSO_ATTEMPTS (PMU_MAX_TRANSITION_DLY_USEC / KSO_WAIT_US)
#define BRCMF_SDIO_MAX_ACCESS_ERRORS 5

#define BC_CORE_POWER_CONTROL_RELOAD 0x2
#define BC_CORE_POWER_CONTROL_SHIFT 13

// Maximum number of frames to transmit in one transaction. Value based on vendor driver.
static constexpr int kMaxTxGlomFrames = 36;

// The size of the internal DMA buffer used for small transfers, events, and transfers before we
// have received any RX space, such as for firmware downloading.
constexpr size_t kDmaInternalBufferSize = 1 << 21;
// The size of each buffer inside the internal DMA buffer, start with the maximum possible
// control frame and add sufficient space for padding.
constexpr size_t kInternalVmoBufferSize = BRCMF_DCMD_MAXLEN + 1024;

// The VMO ID used for the internal DMA buffer. The network device implementation seems to start at
// 0 so if we pick a number from the other end of the spectrum we should be OK.
constexpr uint32_t kInternalVmoId = MAX_VMOS - 1;

#if !defined(NDEBUG)
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
#endif /* !defined(NDEBUG) */

struct sdpcm_shared {
  uint32_t flags;
  uint32_t trap_addr;
  uint32_t assert_exp_addr;
  uint32_t assert_file_addr;
  uint32_t assert_line;
#ifdef BRCMF_CONSOLE_LOG
  uint32_t console_addr; /* Address of struct rte_console */
#endif                   // BRCMF_CONSOLE_LOG
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
#ifdef BRCMF_CONSOLE_LOG
  uint32_t console_addr; /* Address of struct rte_console */
#endif                   // BRCMF_CONSOLE_LOG
  uint32_t msgtrace_addr;
  uint8_t tag[32];
  uint32_t brpt_addr;
};

/* clkstate */
#define CLK_NONE 0
#define CLK_SDONLY 1
#define CLK_PENDING 2
#define CLK_AVAIL 3

#if !defined(NDEBUG)
static size_t qcount[NUMPRIO];
#endif /* !defined(NDEBUG) */

#define DEFAULT_SDIO_DRIVE_STRENGTH 6 /* in milliamps */

#define RETRYCHAN(chan) ((chan) == SDPCM_EVENT_CHANNEL)

/* Limit on rounding up frames */
static const uint16_t max_roundup = 512;

enum brcmf_sdio_frmtype {
  BRCMF_SDIO_FT_NORMAL,
  BRCMF_SDIO_FT_SUPER,
  BRCMF_SDIO_FT_SUB,
};

#define SDIOD_DRVSTR_KEY(chip, pmu) ((((uint32_t)chip) << 16) | (pmu))

/* SDIO Pad drive strength to select value mappings */
struct sdiod_drive_str {
  uint8_t strength; /* Pad Drive Strength in mA */
  uint8_t sel;      /* Chip-specific select value */
};

/* SDIO Drive Strength to sel value table for PMU Rev 11 (1.8V) */
static const struct sdiod_drive_str sdiod_drvstr_tab1_1v8[] = {
    {32, 0x6}, {26, 0x7}, {22, 0x4}, {16, 0x5}, {12, 0x2}, {8, 0x3}, {4, 0x0}, {0, 0x1}};

/* SDIO Drive Strength to sel value table for PMU Rev 13 (1.8v) */
static const struct sdiod_drive_str sdiod_drive_strength_tab5_1v8[] = {
    {6, 0x7}, {5, 0x6}, {4, 0x5}, {3, 0x4}, {2, 0x2}, {1, 0x1}, {0, 0x0}};

/* SDIO Drive Strength to sel value table for PMU Rev 17 (1.8v) */
static const struct sdiod_drive_str sdiod_drvstr_tab6_1v8[] = {
    {3, 0x3}, {2, 0x2}, {1, 0x1}, {0, 0x0}};

/* SDIO Drive Strength to sel value table for 43143 PMU Rev 17 (3.3V) */
static const struct sdiod_drive_str sdiod_drvstr_tab2_3v3[] = {
    {16, 0x7}, {12, 0x5}, {8, 0x3}, {4, 0x1}};

// Align value to alignment by adjusting it upwards. Requires that alignment
// is a power of two.
template <typename T, typename std::enable_if_t<!std::is_signed<T>::value, bool> = true>
static T brcmf_sdio_align_up(T value, T alignment) {
  // x' = x + (y - 1) & ~(y - 1) yields x aligned to y such that x' >= x
  // without using expensive modulo or division operations at the expense of
  // requiring y is a power of two.
  return (value + (alignment - 1)) & (~(alignment - 1));
}

// Compute the padding needed to align a value to alignment.
template <typename T, typename std::enable_if_t<!std::is_signed<T>::value, bool> = true>
static T brcmf_sdio_align_pad(const T value, const T alignment) {
  return (~value + 1) & (alignment - 1);
}

// Align the data pointer in |frame| such that there is enough room for |headroom| bytes and that
// the address immediately following those |headroom| bytes is aligned to |alignment|. The data
// pointer in |frame| is left pointing to where the |headroom| bytes would start. For example
// if the frame data pointer points to 0x1000 and headroom is 64 and alignment is 256 then the
// data following |headroom| would be positioned at 0x1040 (not aligned to 256). This function would
// align the data following |headroom| to 0x1100. This would then require that the headroom data
// starts at 0x10c0, 64 bytes before 0x1100. After alignment frame.Data() will point to 0x10c0 so
// that it's ready for insertion of a header.
//
// This is important because if we end up reading data into a VMO where the frame data spans
// discontiguous physical pages the transfer will be split up when setting up DMA descriptors. The
// transfer size at the end of a physical page could potentially not align to the SDIO block size if
// we have already read |headroom| bytes. By doing this we ensure that both an initial read of
// |headroom| bytes is safe as well as any following reads.
static void frame_align_data(wlan::drivers::components::Frame& frame, uint32_t headroom,
                             uint32_t alignment) {
  uintptr_t padding = brcmf_sdio_align_pad<uintptr_t>(
      reinterpret_cast<uintptr_t>(frame.Data()) + headroom, alignment);
  frame.ShrinkHead(padding);
}

// Align the data pointer in |frame| to |align|, also align |length| to SDIOD_SIZE_ALIGNMENT and
// set the size of |frame| to the aligned length.
void frame_align(wlan::drivers::components::Frame& frame, uint32_t length, uint32_t align) {
  const auto data = reinterpret_cast<uintptr_t>(frame.Data());
  uintptr_t data_align = brcmf_sdio_align_pad<uintptr_t>(data, align);
  frame.ShrinkHead(static_cast<uint32_t>(data_align));
  frame.SetSize(brcmf_sdio_align_up<uint32_t>(length, SDIOD_SIZE_ALIGNMENT));
}

/* To check if there's window offered */
static bool data_ok(struct brcmf_sdio* bus) {
  return static_cast<uint8_t>(bus->tx_max - bus->tx_seq) != 0 &&
         (static_cast<uint8_t>(bus->tx_max - bus->tx_seq) & 0x80) == 0;
}

/*
 * If ctrl_frame_stat is set, clear it and then call the provided |fn| function. All in a thread
 * safe manner.
 */
void brcmf_sdio_if_ctrl_frame_stat_set(struct brcmf_sdio* bus, std::function<void()> fn) {
  if (bus->ctrl_frame_stat.load()) {
    sdio_claim_host(bus->sdiodev->func1);
    if (bus->ctrl_frame_stat.load()) {
      bus->ctrl_frame_stat.store(false);
      fn();
    }
    sdio_release_host(bus->sdiodev->func1);
  }
}

/*
 * If ctrl_frame_stat is clear call the provided |fn| function. All in a thread safe manner.
 */
void brcmf_sdio_if_ctrl_frame_stat_clear(struct brcmf_sdio* bus, std::function<void()> fn) {
  if (!bus->ctrl_frame_stat.load()) {
    sdio_claim_host(bus->sdiodev->func1);
    if (!bus->ctrl_frame_stat.load()) {
      fn();
    }
    sdio_release_host(bus->sdiodev->func1);
  }
}

static zx_status_t brcmf_sdio_kso_control(struct brcmf_sdio* bus, bool on) {
  uint8_t wr_val = 0;
  uint8_t rd_val, cmp_val, bmask;
  zx_status_t err = ZX_OK;
  int err_cnt = 0;
  int try_cnt = 0;

  BRCMF_DBG(TRACE, "Enter: on=%d", on);

  wr_val = static_cast<uint8_t>(static_cast<uint8_t>(on) << SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT);
  /* 1st KSO write goes to AOS wake up core if device is asleep  */
  brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, wr_val, &err);

  if (on) {
    /* device WAKEUP through KSO:
     * write bit 0 & read back until
     * both bits 0 (kso bit) & 1 (dev on status) are set
     */
    cmp_val = SBSDIO_FUNC1_SLEEPCSR_KSO_MASK | SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK;
    bmask = cmp_val;
    zx_nanosleep(zx_deadline_after(ZX_USEC(2000)));
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
    rd_val = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, &err);
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

    zx_nanosleep(zx_deadline_after(ZX_USEC(KSO_WAIT_US)));
    brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, wr_val, &err);

  } while (try_cnt++ < MAX_KSO_ATTEMPTS);

  if (try_cnt > 2) {
    BRCMF_DBG(SDIO, "try_cnt=%d rd_val=0x%x err=%d", try_cnt, rd_val, err);
  }

  if (try_cnt > MAX_KSO_ATTEMPTS) {
    BRCMF_ERR("max tries: rd_val=0x%x err=%d", rd_val, err);
  }

  return err;
}

#define HOSTINTMASK (I_HMB_SW_MASK | I_CHIPACTIVE)

/* Turn backplane clock on or off */
static zx_status_t brcmf_sdio_htclk(struct brcmf_sdio* bus, bool on, bool pendok) {
  zx_status_t err;
  uint8_t clkctl, clkreq, devctl;
  zx_time_t timeout;

  BRCMF_DBG(SDIO, "Enter");

  clkctl = 0;

  if (bus->sr_enabled) {
    bus->clkstate = (on ? CLK_AVAIL : CLK_SDONLY);
    return ZX_OK;
  }

  if (on) {
    /* Request HT Avail */
    clkreq = bus->alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;

    brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
    if (err != ZX_OK) {
      BRCMF_ERR("HT Avail request error: %d", err);
      return ZX_ERR_IO_REFUSED;
    }

    /* Check current status */
    clkctl = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
    if (err != ZX_OK) {
      BRCMF_ERR("HT Avail read error: %d", err);
      return ZX_ERR_IO_REFUSED;
    }

    /* Go to pending and await interrupt if appropriate */
    if (!SBSDIO_CLKAV(clkctl, bus->alp_only) && pendok) {
      /* Allow only clock-available interrupt */
      devctl = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
      if (err != ZX_OK) {
        BRCMF_ERR("Devctl error setting CA: %d", err);
        return ZX_ERR_IO_REFUSED;
      }

      devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
      brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_DEVICE_CTL, devctl, &err);
      BRCMF_DBG(SDIO, "CLKCTL: set PENDING");
      bus->clkstate = CLK_PENDING;

      return ZX_OK;
    } else if (bus->clkstate == CLK_PENDING) {
      /* Cancel CA-only interrupt filter */
      devctl = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
      devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
      brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_DEVICE_CTL, devctl, &err);
    }

    /* Otherwise, wait here (polling) for HT Avail */
    timeout = zx_clock_get_monotonic() + ZX_USEC(PMU_MAX_TRANSITION_DLY_USEC);
    while (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
      clkctl = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
      if (zx_clock_get_monotonic() > timeout) {
        break;
      } else {
        zx_nanosleep(zx_deadline_after(ZX_USEC(5000)));
      }
    }
    if (err != ZX_OK) {
      BRCMF_ERR("HT Avail request error: %d", err);
      return ZX_ERR_IO_REFUSED;
    }
    if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
      BRCMF_ERR("HT Avail timeout (%d): clkctl 0x%02x", PMU_MAX_TRANSITION_DLY_USEC, clkctl);
      return ZX_ERR_SHOULD_WAIT;
    }

    /* Mark clock available */
    bus->clkstate = CLK_AVAIL;
    BRCMF_DBG(SDIO, "CLKCTL: turned ON");

#if !defined(NDEBUG)
    if (!bus->alp_only) {
      if (SBSDIO_ALPONLY(clkctl)) {
        BRCMF_ERR("HT Clock should be on");
      }
    }
#endif /* !defined(NDEBUG) */

  } else {
    clkreq = 0;

    if (bus->clkstate == CLK_PENDING) {
      /* Cancel CA-only interrupt filter */
      devctl = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
      devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
      brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_DEVICE_CTL, devctl, &err);
    }

    bus->clkstate = CLK_SDONLY;
    brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
    BRCMF_DBG(SDIO, "CLKCTL: turned OFF");
    if (err != ZX_OK) {
      BRCMF_ERR("Failed access turning clock off: %d", err);
      return ZX_ERR_IO_REFUSED;
    }
  }
  return ZX_OK;
}

/* Change idle/active SD state */
static zx_status_t brcmf_sdio_sdclk(struct brcmf_sdio* bus, bool on) {
  BRCMF_DBG(SDIO, "Enter");

  if (on) {
    bus->clkstate = CLK_SDONLY;
  } else {
    bus->clkstate = CLK_NONE;
  }

  return ZX_OK;
}

/* Transition SD and backplane clock readiness */
static zx_status_t brcmf_sdio_clkctl(struct brcmf_sdio* bus, uint target, bool pendok) {
  uint oldstate = bus->clkstate;

  BRCMF_DBG(SDIO, "Enter");

  /* Early exit if we're already there */
  if (bus->clkstate == target) {
    return ZX_OK;
  }
  if (bus->ci->chip == BRCM_CC_4359_CHIP_ID && target == CLK_NONE) {
    BRCMF_DBG(TEMP, "Returning because chip is 4359");
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
        BRCMF_ERR("request for %d -> %d", bus->clkstate, target);
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
  BRCMF_DBG(SDIO, "%d -> %d", oldstate, bus->clkstate);

  return ZX_OK;
}

static zx_status_t brcmf_sdio_bus_sleep(struct brcmf_sdio* bus, bool sleep, bool pendok) {
  zx_status_t err = ZX_OK;
  uint8_t clkcsr;

  BRCMF_DBG(SDIO, "Enter: request %s currently %s", (sleep ? "SLEEP" : "WAKE"),
            (bus->sleeping ? "SLEEP" : "WAKE"));

  /* If SR is enabled control bus state with KSO */
  if (bus->sr_enabled) {
    /* Done if we're already in the requested state */
    if (sleep == bus->sleeping) {
      goto end;
    }

    /* Going to sleep */
    if (sleep) {
      clkcsr = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
      if ((clkcsr & SBSDIO_CSR_MASK) == 0) {
        BRCMF_DBG(SDIO, "no clock, set ALP");
        brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_ALP_AVAIL_REQ, &err);
      }
      err = brcmf_sdio_kso_control(bus, false);
    } else {
      err = brcmf_sdio_kso_control(bus, true);
    }
    if (err != ZX_OK) {
      BRCMF_ERR("error while changing bus sleep state %d", err);
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
  BRCMF_DBG(SDIO, "new state %s", (sleep ? "SLEEP" : "WAKE"));
done:
  BRCMF_DBG(SDIO, "Exit: err=%d", err);
  return err;
}

static inline bool brcmf_sdio_valid_shared_address(uint32_t addr) {
  return !(addr == 0 || ((~addr >> 16) & 0xffff) == (addr & 0xffff));
}

static zx_status_t __brcmf_sdio_sharedrw(struct brcmf_sdio* bus, struct sdpcm_shared* sh,
                                         bool write) {
  uint32_t addr = 0;
  zx_status_t rv;
  uint32_t shaddr = 0;

  if (write) {
    if (sh == nullptr) {
      BRCMF_ERR("Input sdpcm pointer doesn't exist");
      return ZX_ERR_INVALID_ARGS;
    }
    if ((sh->flags & SDPCM_SHARED_VERSION_MASK) > SDPCM_SHARED_VERSION) {
      BRCMF_ERR("sdpcm shared version unsupported: dhd %d dongle %d", SDPCM_SHARED_VERSION,
                sh->flags & SDPCM_SHARED_VERSION_MASK);
      return ZX_ERR_WRONG_TYPE;
    }
  }

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
  rv = brcmf_sdiod_ramrw(bus->sdiodev, false, shaddr, (uint8_t*)&addr, 4);
  if (rv != ZX_OK) {
    goto fail;
  }

  /*
   * Check if addr is valid.
   * NVRAM length at the end of memory should have been overwritten.
   */
  if (!brcmf_sdio_valid_shared_address(addr)) {
    BRCMF_ERR("invalid sdpcm_shared address 0x%08X", addr);
    rv = ZX_ERR_INVALID_ARGS;
    goto fail;
  }

  BRCMF_DBG(SDIO, "sdpcm_shared address 0x%08X", addr);

  /* Read or write hndrte_shared structure */
  rv = brcmf_sdiod_ramrw(bus->sdiodev, write, addr, (uint8_t*)sh, sizeof(struct sdpcm_shared_le));
  if (rv != ZX_OK) {
    goto fail;
  }

  sdio_release_host(bus->sdiodev->func1);

  if (!write && (sh->flags & SDPCM_SHARED_VERSION_MASK) > SDPCM_SHARED_VERSION) {
    BRCMF_ERR("sdpcm shared version unsupported: dhd %d dongle %d", SDPCM_SHARED_VERSION,
              sh->flags & SDPCM_SHARED_VERSION_MASK);
    return ZX_ERR_WRONG_TYPE;
  }
  return ZX_OK;

fail:
  BRCMF_ERR("unable to %s sdpcm_shared info: rv=%d (addr=0x%x)", write ? "write" : "read", rv,
            addr);
  sdio_release_host(bus->sdiodev->func1);
  return rv;
}

static zx_status_t brcmf_sdio_shared_read(struct brcmf_sdio* bus, struct sdpcm_shared* sh) {
  return __brcmf_sdio_sharedrw(bus, sh, false);
}

static zx_status_t brcmf_sdio_shared_write(struct brcmf_sdio* bus, struct sdpcm_shared* sh) {
  return __brcmf_sdio_sharedrw(bus, sh, true);
}

#ifdef BRCMF_CONSOLE_LOG
static void brcmf_sdio_get_console_addr(struct brcmf_sdio* bus) {
  struct sdpcm_shared sh;

  if (brcmf_sdio_shared_read(bus, &sh) == ZX_OK) {
    bus->console_addr = sh.console_addr;
  }
}
#else   // BRCMF_CONSOLE_LOG
static void brcmf_sdio_get_console_addr(struct brcmf_sdio* bus) {}
#endif  // BRCMF_CONSOLE_LOG

static uint32_t brcmf_sdio_hostmail(struct brcmf_sdio* bus) {
  struct brcmf_sdio_dev* sdiod = bus->sdiodev;
  struct brcmf_core* core = bus->sdio_core;
  uint32_t intstatus = 0;
  uint32_t hmb_data;
  uint8_t fcbits;
  zx_status_t ret;

  BRCMF_DBG(SDIO, "Enter");

  /* Read mailbox data and ack that we did so */
  hmb_data = brcmf_sdiod_func1_rl(sdiod, core->base + SD_REG(tohostmailboxdata), &ret);

  if (ret == ZX_OK) {
    brcmf_sdiod_func1_wl(sdiod, core->base + SD_REG(tosbmailbox), SMB_INT_ACK, &ret);
  }

  bus->sdcnt.f1regdata += 2;

  /* dongle indicates the firmware has halted/crashed */
  if (hmb_data & HMB_DATA_FWHALT) {
    BRCMF_ERR("mailbox indicates firmware halted");
  }

  /* Dongle recomposed rx frames, accept them again */
  if (hmb_data & HMB_DATA_NAKHANDLED) {
    BRCMF_DBG(SDIO, "Dongle reports NAK handled, expect rtx of %d", bus->rx_seq);
    if (!bus->rxskip) {
      BRCMF_ERR("unexpected NAKHANDLED!");
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
      BRCMF_ERR(
          "Version mismatch, dongle reports %d, "
          "expecting %d",
          bus->sdpcm_ver, SDPCM_PROT_VERSION);
    } else {
      BRCMF_DBG(SDIO, "Dongle ready, protocol version %d", bus->sdpcm_ver);
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
    BRCMF_ERR("Unknown mailbox data content: 0x%02x", hmb_data);
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

  BRCMF_WARN_THROTTLE("%sterminate frame%s", abort ? "abort command, " : "",
                      rtx ? ", send NAK" : "");

  if (abort) {
    brcmf_sdiod_abort(bus->sdiodev, SDIO_FN_2);
  }

  brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_FRAMECTRL, SFC_RF_TERM, &err);
  bus->sdcnt.f1regdata++;

  /* Wait until the packet has been flushed (device/FIFO stable) */
  for (lastrbc = retries = 0xffff; retries > 0; retries--) {
    hi = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_RFRAMEBCHI, &err);
    lo = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_RFRAMEBCLO, &err);
    bus->sdcnt.f1regdata += 2;

    if ((hi == 0) && (lo == 0)) {
      break;
    }

    if ((hi > (lastrbc >> 8)) && (lo > (lastrbc & 0x00ff))) {
      BRCMF_ERR("count growing: last 0x%04x now 0x%04x", lastrbc, (hi << 8) + lo);
    }
    const auto lastrbc_unsized = (hi << 8) + lo;
    ZX_DEBUG_ASSERT(lastrbc_unsized <= std::numeric_limits<uint16_t>::max());
    lastrbc = static_cast<uint16_t>(lastrbc_unsized);
  }

  if (!retries) {
    BRCMF_ERR("count never zeroed: last 0x%04x", lastrbc);
  } else {
    BRCMF_DBG_THROTTLE(SDIO, "flush took %d iterations", 0xffff - retries);
  }

  if (rtx) {
    bus->sdcnt.rxrtx++;
    brcmf_sdiod_func1_wl(sdiod, core->base + SD_REG(tosbmailbox), SMB_NAK, &err);

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
  BRCMF_ERR("sdio error, abort command and terminate frame");
  bus->sdcnt.tx_sderrs++;

  brcmf_sdiod_abort(sdiodev, SDIO_FN_2);
  brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_FRAMECTRL, SFC_WF_TERM, NULL);
  bus->sdcnt.f1regdata++;

  for (i = 0; i < 3; i++) {
    hi = brcmf_sdiod_func1_rb(sdiodev, SBSDIO_FUNC1_WFRAMEBCHI, NULL);
    lo = brcmf_sdiod_func1_rb(sdiodev, SBSDIO_FUNC1_WFRAMEBCLO, NULL);
    bus->sdcnt.f1regdata += 2;
    if ((hi == 0) && (lo == 0)) {
      break;
    }
  }
  // TODO(fxbug.dev/49955) There is a firmware issue that will eventually cause TX CRC errors when
  // using TX glomming. After this happens any attempt at TX glomming will fail. However, sending a
  // single frame per transaction still works. So fall back to that when get a TX error. In most
  // normal cases we still get the benefit of TX glomming and in some high throughput cases we will
  // fall back to behaving as if TX glomming wasn't enabled.
  bus->txbound = 1;
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

  // trace_brcmf_sdpcm_hdr(SDPCM_RX, header);

  /* hw header */
  len = *(uint16_t*)header;
  checksum = *(uint16_t*)(header + sizeof(uint16_t));
  /* All zero means no more to read */
  if (!(len | checksum)) {
    bus->rxpending = false;
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if ((uint16_t)(~(len ^ checksum))) {
    BRCMF_ERR("HW header checksum error, len = 0x%04x, checksum = 0x%04x", len, checksum);
    bus->sdcnt.rx_badhdr++;
    brcmf_sdio_rxfail(bus, false, false);
    return ZX_ERR_IO;
  }
  if (len < SDPCM_HDRLEN) {
    BRCMF_ERR("HW header length error");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (type == BRCMF_SDIO_FT_SUPER && (brcmf_sdio_align_up(len, bus->blocksize) != rd->len)) {
    BRCMF_ERR("HW superframe header length error, rd->len = %u, len = %u, aligned length = %u",
              rd->len, len, brcmf_sdio_align_up(len, bus->blocksize));
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (type == BRCMF_SDIO_FT_SUB && len > rd->len) {
    BRCMF_ERR("HW subframe header length error, len = %u, rd->len = %u", len, rd->len);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  rd->len = len;

  /* software header */
  header += SDPCM_HWHDR_LEN;
  swheader = *(uint32_t*)header;
  if (type == BRCMF_SDIO_FT_SUPER && SDPCM_GLOMDESC(header)) {
    BRCMF_ERR("Glom descriptor found in superframe head");
    rd->len = 0;
    return ZX_ERR_INVALID_ARGS;
  }
  rx_seq = (uint8_t)(swheader & SDPCM_SEQ_MASK);
  rd->channel = (swheader & SDPCM_CHANNEL_MASK) >> SDPCM_CHANNEL_SHIFT;
  if (len > MAX_RX_DATASZ && rd->channel != SDPCM_CONTROL_CHANNEL && type != BRCMF_SDIO_FT_SUPER) {
    BRCMF_WARN_THROTTLE("HW header length too long");
    bus->sdcnt.rx_toolong++;
    brcmf_sdio_rxfail(bus, false, false);
    rd->len = 0;
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (type == BRCMF_SDIO_FT_SUPER && rd->channel != SDPCM_GLOM_CHANNEL) {
    BRCMF_ERR("Wrong channel for superframe");
    rd->len = 0;
    return ZX_ERR_INVALID_ARGS;
  }
  if (type == BRCMF_SDIO_FT_SUB && rd->channel != SDPCM_DATA_CHANNEL &&
      rd->channel != SDPCM_EVENT_CHANNEL) {
    BRCMF_ERR("Wrong channel for subframe");
    rd->len = 0;
    return ZX_ERR_INVALID_ARGS;
  }
  rd->dat_offset = brcmf_sdio_getdatoffset(header);
  if (rd->dat_offset < SDPCM_HDRLEN || rd->dat_offset > rd->len) {
    BRCMF_ERR("seq %d: bad data offset", rx_seq);
    bus->sdcnt.rx_badhdr++;
    brcmf_sdio_rxfail(bus, false, false);
    rd->len = 0;
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (rd->seq_num != rx_seq) {
    BRCMF_DBG(SDIO, "seq %d, expected %d", rx_seq, rd->seq_num);
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
      BRCMF_ERR("seq %d: next length error", rx_seq);
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
    BRCMF_ERR("tx_seq_max is %d, bus->tx_seq is %d: max tx seq number error", tx_seq_max,
              bus->tx_seq);
    bus->sdiodev->drvr->device->GetInspect()->LogSdioMaxTxSeqErr();
    tx_seq_max = bus->tx_seq + 2;
  }
  bus->tx_max = tx_seq_max;

  return ZX_OK;
}

static inline void brcmf_sdio_update_hwhdr(uint8_t* header, uint16_t frm_length) {
  auto hw_hdr = reinterpret_cast<uint16_t*>(header);
  hw_hdr[0] = frm_length;
  hw_hdr[1] = ~frm_length;
}

static void brcmf_sdio_hdpack(struct brcmf_sdio* bus, uint8_t* header,
                              struct brcmf_sdio_hdrinfo* hd_info) {
  uint32_t hdrval;
  uint8_t hdr_offset;

  brcmf_sdio_update_hwhdr(header, hd_info->len);
  hdr_offset = SDPCM_HWHDR_LEN;

  if (bus->txglom) {
    // Insert hardware extension header if TX glomming is enabled.
    auto hw_ext_hdr = reinterpret_cast<uint32_t*>(header + hdr_offset);
    hw_ext_hdr[0] = (hd_info->len - hdr_offset) | (hd_info->lastfrm << 24);
    hw_ext_hdr[1] = hd_info->tail_pad << 16;
    hdr_offset += SDPCM_HWEXT_LEN;
  }

  // Software header
  hdrval = hd_info->seq_num;
  hdrval |= (hd_info->channel << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK;
  hdrval |= (hd_info->dat_offset << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK;
  auto sw_hdr = reinterpret_cast<uint32_t*>(header + hdr_offset);
  sw_hdr[0] = hdrval;
  sw_hdr[1] = 0;
}

static zx_status_t brcmf_sdio_prepare_rxglom_frames(struct brcmf_sdio* bus) {
  // We have a valid glom descriptor, generate frames to receive into
  TRACE_DURATION("brcmfmac:isr", "glom_desc");

  wlan::drivers::components::Frame glom_desc = std::move(bus->rx_glom->glom_desc);
  bus->rx_glom->glom_desc.SetSize(0);
  bus->cur_read.len = 0;

  const uint32_t length = glom_desc.Size();
  uint32_t num_entries = length / 2;
  if (length == 0 || (length & 0x01) != 0) {
    BRCMF_ERR("Bad glom descriptor length %u, ignoring descriptor", length);
    return ZX_ERR_INTERNAL;
  }
  auto glom_data = reinterpret_cast<const uint16_t*>(glom_desc.Data());

  bus->rx_glom->glom_size = 0;
  bus->rx_glom->glom_count = 0;

  wlan::drivers::components::FrameContainer glom_frames =
      brcmf_sdio_acquire_rx_space(bus, num_entries);
  if (glom_frames.size() != num_entries) {
    // TODO(https://fxbug.dev/102298): Consider this line for detect rule about lack of RX buffers
    BRCMF_WARN_THROTTLE("Failed to acquire RX space for %u glom entries", num_entries);
    ++bus->sdcnt.rx_outofbufs;
    // Attempt to acquire internal RX space to be able to receive commands and events.
    glom_frames = brcmf_sdio_acquire_internal_rx_space(bus, num_entries);
    if (glom_frames.size() != num_entries) {
      return ZX_ERR_NO_RESOURCES;
    }
  }

  uint16_t total_len = 0;
  auto frame = glom_frames.begin();
  for (uint32_t entry = 0; entry < num_entries; ++entry, ++frame) {
    uint16_t sub_frame_len = glom_data[entry];
    if (sub_frame_len < SDPCM_HDRLEN || (entry == 0 && sub_frame_len < 2 * SDPCM_HDRLEN)) {
      BRCMF_ERR("Descriptor %u has bad length %u", entry, sub_frame_len);
      return ZX_ERR_INTERNAL;
    }
    total_len += sub_frame_len;

    if (entry + 1 == num_entries) {
      // For the last frame we adjust the read length so that the total length of the entire glom
      // transfer is a multiple of the SDIO block size.
      uint16_t aligned_size = brcmf_sdio_align_up(total_len, bus->blocksize);
      sub_frame_len += aligned_size - total_len;
      total_len = aligned_size;
    }

    // Store the original frame capacity here so we can check to see if we exceeded it after
    // calculating the subframe length.
    uint32_t capacity = frame->Size();

    frame_align(*frame, sub_frame_len, bus->sgentry_align);
    bus->rx_glom->glom_size += frame->Size();
    ++bus->rx_glom->glom_count;

    if (frame->Size() > capacity) {
      // This subframe is larger than the capacity of the frame we acquired. This can only happen
      // for events, the acquired frame size should have been set to exceed the maximum possible
      // size of a data frame. This means that we do not need to ever return this frame to
      // netstack so it's safe to use an internal frame that should have the required capacity.
      std::optional<wlan::drivers::components::Frame> internal_frame =
          brcmf_sdio_acquire_single_internal_rx_space(bus);
      if (!internal_frame) {
        BRCMF_ERR("Failed to acquire internal frame for event in RX glom chain");
        return ZX_ERR_NO_RESOURCES;
      }
      capacity = internal_frame->Size();

      // Replace the acquired frame with our internal frame, the acquired frame will be returned to
      // storage when it's moved to.
      *frame = std::move(*internal_frame);
      // Make sure to align this new frame properly.
      frame_align(*frame, sub_frame_len, bus->sgentry_align);
      if (frame->Size() > capacity) {
        BRCMF_ERR("Required subframe length %u in RX glom chain exceeds maximum capacity %u",
                  frame->Size(), capacity);
        return ZX_ERR_INTERNAL;
      }
    }
  }

  // At this point our glom chain is complete, move it to the rx_glom struct so the rest of the code
  // can pick it up.
  bus->rx_glom->glom_frames = std::move(glom_frames);

  return ZX_OK;
}

static uint8_t brcmf_sdio_rxglom_frames(struct brcmf_sdio* bus, uint8_t rxseq) {
  TRACE_DURATION("brcmfmac:isr", "sdio_rxglom_frames");

  if (bus->rx_glom->glom_desc.Size() > 0) {
    zx_status_t status = brcmf_sdio_prepare_rxglom_frames(bus);
    if (status != ZX_OK) {
      brcmf_sdio_rxfail(bus, false, false);
      return 0;
    }
  }

  if (bus->rx_glom->glom_frames.empty()) {
    return 0;
  }

  auto glom_frames = std::move(bus->rx_glom->glom_frames);
  bus->rx_glom->glom_frames.clear();

  zx_status_t err;
  {
    TRACE_DURATION("brcmfmac:isr", "recv_frames", "bytes", TA_UINT64(bus->rx_glom->glom_size),
                   "count", TA_UINT32(bus->rx_glom->glom_count));
    err = brcmf_sdiod_recv_frames(bus->sdiodev,
                                  cpp20::span<wlan::drivers::components::Frame>(glom_frames));
  }
  ++bus->sdcnt.f2rxdata;

  TRACE_DURATION("brcmfmac:isr", "post_recv");

  if (err != ZX_OK) {
    BRCMF_ERR("Failed to receive frames: %s", zx_status_get_string(err));
    brcmf_sdio_rxfail(bus, true, false);
    ++bus->sdcnt.rxglomfail;
    return 0;
  }

  struct brcmf_sdio_hdrinfo updated_rd;
  updated_rd.seq_num = rxseq;
  updated_rd.len = bus->rx_glom->glom_size;
  err = brcmf_sdio_hdparse(bus, glom_frames.begin()->Data(), &updated_rd, BRCMF_SDIO_FT_SUPER);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to parse superframe header: %s", zx_status_get_string(err));
    brcmf_sdio_rxfail(bus, true, false);
    ++bus->sdcnt.rxglomfail;
    bus->cur_read.len = 0;
    return 0;
  }

  bus->cur_read.len = static_cast<uint16_t>(updated_rd.len_nxtfrm << 4);

  glom_frames.begin()->ShrinkHead(updated_rd.dat_offset);

  TRACE_DURATION("brcmfmac:isr", "frame loop");

  uint32_t count = 0;
  for (auto frame = glom_frames.begin(); frame != glom_frames.end(); ++count, ++frame) {
    uint8_t* const data = frame->Data();

    updated_rd.len = frame->Size();
    updated_rd.seq_num = rxseq++;
    err = brcmf_sdio_hdparse(bus, data, &updated_rd, BRCMF_SDIO_FT_SUB);
    if (err != ZX_OK) {
      BRCMF_ERR("Failed to parse subframe header: %s", zx_status_get_string(err));
      brcmf_sdio_rxfail(bus, true, false);
      ++bus->sdcnt.rxglomfail;
      bus->cur_read.len = 0;
      return 0;
    }

    const uint16_t sub_frame_len = *reinterpret_cast<uint16_t*>(data);
    const uint8_t data_offset = brcmf_sdio_getdatoffset(data + SDPCM_HWHDR_LEN);

    frame->SetSize(sub_frame_len);
    frame->ShrinkHead(data_offset);
    if (frame->Size() == 0) {
      continue;
    }

    if (brcmf_sdio_fromevntchan(data + SDPCM_HWHDR_LEN)) {
      brcmf_rx_event(bus->sdiodev->drvr, std::move(*frame));
      // Set the size to zero here so that it won't be received by netstack
      frame->SetSize(0);
      continue;
    }
    ++bus->sdcnt.rxglompkts;
  }

  TRACE_DURATION("brcmfmac:isr", "delivering");

  brcmf_rx_frames(bus->sdiodev->drvr, std::move(glom_frames));
  ++bus->sdcnt.rxglomframes;

  return count;
}

static int brcmf_sdio_dcmd_resp_wait(struct brcmf_sdio* bus, bool* pending) {
  /* Wait until control frame is available */
  *pending = false;  // TODO(cphoenix): Does signal_pending() have meaning in Garnet?
  zx_status_t result;
  result = sync_completion_wait(&bus->dcmd_resp_wait, ZX_MSEC(DCMD_RESP_TIMEOUT_MSEC));
  sync_completion_reset(&bus->dcmd_resp_wait);
  return result;
}

static zx_status_t brcmf_sdio_dcmd_resp_wake(struct brcmf_sdio* bus) {
  sync_completion_signal(&bus->dcmd_resp_wait);

  return ZX_OK;
}

static void brcmf_sdio_return_control_frame(struct brcmf_sdio* bus,
                                            wlan::drivers::components::Frame&& frame) {
  if (frame.Size() == 0) {
    BRCMF_ERR("Control frame empty");
    return;
  }

  {
    std::lock_guard lock(bus->sdiodev->drvr->irq_callback_lock);
    if (!bus->rx_ctl_frame.empty()) {
      BRCMF_ERR("Last control frame is being processed.");
      return;
    }
    bus->rx_ctl_frame.resize(frame.Size());
    memcpy(bus->rx_ctl_frame.data(), frame.Data(), frame.Size());
  }
  brcmf_sdio_dcmd_resp_wake(bus);
}

static void brcmf_sdio_read_control_frame(struct brcmf_sdio* bus,
                                          wlan::drivers::components::Frame&& frame, uint32_t len,
                                          uint32_t doff) {
  uint32_t rdlen;
  if (len <= BRCMF_FIRSTREAD) {
    // We don't need to read more data but we have to make sure frame is valid
    ZX_ASSERT(len >= doff);
    frame.SetSize(len);
    frame.ShrinkHead(doff);
    brcmf_sdio_return_control_frame(bus, std::move(frame));
    return;
  }

  // Control frames need to use the internal RX space because they may exceed the space available
  // in the RX space provided by upper layers.
  std::optional<wlan::drivers::components::Frame> ctl_frame_opt =
      brcmf_sdio_acquire_single_internal_rx_space(bus);
  if (!ctl_frame_opt) {
    BRCMF_ERR("Failed to acquire space for control frame");
    return;
  }
  wlan::drivers::components::Frame& ctl_frame = ctl_frame_opt.value();
  // Ensure that the frame data pointer after the first BRCMF_FIRSTREAD is aligned to block size
  frame_align_data(ctl_frame, BRCMF_FIRSTREAD, bus->sdiodev->func2->blocksize);
  memcpy(ctl_frame.Data(), frame.Data(), BRCMF_FIRSTREAD);
  ctl_frame.ShrinkHead(BRCMF_FIRSTREAD);

  rdlen = len - BRCMF_FIRSTREAD;
  if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
    uint32_t pad = brcmf_sdio_align_pad<uint32_t>(rdlen, bus->blocksize);
    if (pad <= bus->roundup && pad < bus->blocksize && len + pad < bus->sdiodev->bus_if->maxctl) {
      rdlen += pad;
    }
  } else {
    rdlen = brcmf_sdio_align_up<uint32_t>(rdlen, bus->head_align);
  }

  if ((rdlen + BRCMF_FIRSTREAD) > bus->sdiodev->bus_if->maxctl) {
    BRCMF_ERR("%u byte control read exceeds %u byte buffer", rdlen, bus->sdiodev->bus_if->maxctl);
    brcmf_sdio_rxfail(bus, false, false);
    return;
  }

  if ((len - doff) > bus->sdiodev->bus_if->maxctl) {
    BRCMF_ERR("%u byte ctl frame (%u byte ctl data) exceeds %u byte limit", len, len - doff,
              bus->sdiodev->bus_if->maxctl);
    bus->sdcnt.rx_toolong++;
    brcmf_sdio_rxfail(bus, false, false);
    return;
  }

  zx_status_t err = brcmf_sdiod_recv_frame(bus->sdiodev, ctl_frame, rdlen);
  bus->sdcnt.f2rxdata++;
  if (err != ZX_OK) {
    BRCMF_ERR("Read %u control bytes failed: %s", rdlen, zx_status_get_string(err));
    bus->sdcnt.rxc_errors++;
    brcmf_sdio_rxfail(bus, true, true);
    return;
  }

  ctl_frame.GrowHead(BRCMF_FIRSTREAD);
  ctl_frame.ShrinkHead(doff);
  ctl_frame.SetSize(len - doff);

  brcmf_sdio_return_control_frame(bus, std::move(ctl_frame));
}

/* Pad read to blocksize for efficiency */
static void brcmf_sdio_pad(struct brcmf_sdio* bus, uint16_t* rdlen) {
  if (bus->roundup && bus->blocksize && *rdlen > bus->blocksize) {
    uint16_t pad = brcmf_sdio_align_pad(*rdlen, bus->blocksize);
    if (pad <= bus->roundup && pad < bus->blocksize &&
        *rdlen + pad + BRCMF_FIRSTREAD < MAX_RX_DATASZ) {
      *rdlen += pad;
    }
  } else {
    *rdlen = brcmf_sdio_align_up(*rdlen, bus->head_align);
  }
}

static bool bus_rx_ok(struct brcmf_sdio* bus) {
  return bus->rxpending && !bus->rxskip && bus->sdiodev->state == BRCMF_SDIOD_DATA;
}

static zx_status_t brcmf_sdio_tx_ctrlframe(struct brcmf_sdio* bus, uint8_t* frame, uint16_t len);

static zx_status_t brcmf_sdio_process_ctrl_tx(struct brcmf_sdio* bus) {
  zx_status_t err = ZX_OK;
  if ((bus->clkstate == CLK_AVAIL) && data_ok(bus)) {
    brcmf_sdio_if_ctrl_frame_stat_set(bus, [&bus, &err]() {
      err = brcmf_sdio_tx_ctrlframe(bus, bus->ctrl_frame_buf, bus->ctrl_frame_len);
      bus->ctrl_frame_err = err;
      std::atomic_thread_fence(std::memory_order_seq_cst);
      brcmf_sdio_wait_event_wakeup(bus);
    });
  }
  return err;
}

static uint32_t brcmf_sdio_read_frames(struct brcmf_sdio* bus, uint32_t max_frames) {
  TRACE_DURATION("brcmfmac:isr", "read_frames");

  struct brcmf_sdio_hdrinfo* const rd = &bus->cur_read;

  bus->rxpending = true;

  uint32_t rxleft = max_frames;
  for (rd->seq_num = bus->rx_seq; rxleft > 0 && bus_rx_ok(bus); ++rd->seq_num, --rxleft) {
    if (bus->rx_glom->glom_desc.Size() > 0) {
      // RX glom
      uint32_t count = brcmf_sdio_rxglom_frames(bus, rd->seq_num);
      if (count > 0) {
        rd->seq_num += count - 1;
      }
      rxleft = (rxleft > count) ? (rxleft - count) : 1;
      continue;
    }

    std::optional<wlan::drivers::components::Frame> frame = brcmf_sdio_acquire_single_rx_space(bus);
    if (!frame) {
      // TODO(https://fxbug.dev/102298): Consider this line for detect rule about lack of RX
      // buffers
      BRCMF_WARN_THROTTLE("Failed to acquire frame for RX");
      if (rd->len == 0) {
        // Only count header read fails if we were going to do a header-only read below.
        ++bus->sdcnt.rx_hdrfail;
      }
      ++bus->sdcnt.rx_outofbufs;
      // Attempt to use internal RX space instead, this allows processing of commands and events
      // at least.
      frame = brcmf_sdio_acquire_single_internal_rx_space(bus);
      if (!frame) {
        // RX fail will also log a message so the combination of the log message above and RX fail
        // will indicate that both attempts to acquire frames failed.
        brcmf_sdio_rxfail(bus, false, false);
        continue;
      }
    }

    rd->len_left = rd->len;
    uint32_t head_read = 0;
    if (rd->len == 0) {
      TRACE_DURATION("brcmfmac:isr", "read header");

      // Ensure that the data pointer after BRCMF_FIRSTREAD is aligned to the SDIO block size.
      frame_align_data(*frame, BRCMF_FIRSTREAD, bus->sdiodev->func2->blocksize);

      // If we don't know how much to read then read headers first.
      zx_status_t result = brcmf_sdiod_recv_frame(bus->sdiodev, *frame, BRCMF_FIRSTREAD);
      if (result != ZX_OK) {
        BRCMF_ERR("RXHEADER FAILED: %s", zx_status_get_string(result));
        ++bus->sdcnt.rx_hdrfail;
        brcmf_sdio_rxfail(bus, true, true);
        continue;
      }

      if (brcmf_sdio_hdparse(bus, frame->Data(), rd, BRCMF_SDIO_FT_NORMAL) != ZX_OK) {
        continue;
      }

      if (rd->channel == SDPCM_CONTROL_CHANNEL) {
        brcmf_sdio_read_control_frame(bus, std::move(*frame), rd->len, rd->dat_offset);
        uint32_t next_len = rd->len_nxtfrm << 4;
        ZX_DEBUG_ASSERT(next_len <= std::numeric_limits<uint16_t>::max());
        rd->len = static_cast<uint16_t>(next_len);
        rd->len_nxtfrm = 0;
        // Treat all packets as event if we don't know
        rd->channel = SDPCM_EVENT_CHANNEL;
        continue;
      }
      rd->len_left = rd->len > BRCMF_FIRSTREAD ? rd->len - BRCMF_FIRSTREAD : 0;
      head_read = BRCMF_FIRSTREAD;
      frame->ShrinkHead(head_read);
    }

    brcmf_sdio_pad(bus, &rd->len_left);

    frame_align(*frame, rd->len_left, bus->head_align);

    if (frame->Size() > 0) {
      TRACE_DURATION("brcmfmac:isr", "read frame");

      zx_status_t err = brcmf_sdiod_recv_frame(bus->sdiodev, *frame, frame->Size());
      ++bus->sdcnt.f2rxdata;
      if (err != ZX_OK) {
        BRCMF_ERR("read %u bytes from channel %d failed: %s", rd->len, rd->channel,
                  zx_status_get_string(err));
        brcmf_sdio_rxfail(bus, true, RETRYCHAN(rd->channel));
        continue;
      }
    }

    if (head_read > 0) {
      // We read the header separetely into the start of the frame, grow the head to expose the
      // header data.
      frame->GrowHead(head_read);
    } else {
      // We read both header and data in one operation.
      struct brcmf_sdio_hdrinfo rd_new;
      rd_new.seq_num = rd->seq_num;
      if (brcmf_sdio_hdparse(bus, frame->Data(), &rd_new, BRCMF_SDIO_FT_NORMAL) != ZX_OK) {
        rd->len = 0;
        continue;
      }
      ++bus->sdcnt.rx_readahead_cnt;
      if (rd->len != brcmf_sdio_align_up<uint16_t>(rd_new.len, 16u)) {
        BRCMF_ERR("Frame length mismatch, read %u, should be %u", rd->len,
                  ZX_ROUNDUP(rd_new.len, 16));
        rd->len = 0;
        brcmf_sdio_rxfail(bus, true, true);
        continue;
      }
      rd->len_nxtfrm = rd_new.len_nxtfrm;
      rd->channel = rd_new.channel;
      rd->dat_offset = rd_new.dat_offset;

      if (rd_new.channel == SDPCM_CONTROL_CHANNEL) {
        BRCMF_ERR("Readahead on control packet %u?", rd_new.seq_num);
        rd->len = 0;
        brcmf_sdio_rxfail(bus, false, true);
        continue;
      }
    }

    if (rd->channel == SDPCM_GLOM_CHANNEL) {
      TRACE_DURATION("brcmfmac:isr", "glom_desc");

      // Glom descriptor for receiving multiple frames
      if (SDPCM_GLOMDESC(&frame->Data()[SDPCM_HWHDR_LEN])) {
        frame->SetSize(rd->len);
        frame->ShrinkHead(SDPCM_HDRLEN);
        bus->rx_glom->glom_desc = std::move(*frame);
        ZX_ASSERT(frame->Size() == bus->rx_glom->glom_desc.Size());
      } else {
        BRCMF_WARN_THROTTLE("glom superframe without descriptor");
        brcmf_sdio_rxfail(bus, false, false);
      }
      // Prepare the descriptor for the next read.
      rd->len = rd->len_nxtfrm << 4;
      rd->len_nxtfrm = 0;
      // Treat all packets as events if we don't know.
      rd->channel = SDPCM_EVENT_CHANNEL;
      continue;
    }

    frame->SetSize(rd->len);
    frame->ShrinkHead(rd->dat_offset);

    if (frame->Size() > 0) {
      TRACE_DURATION("brcmfmac:isr", "deliver frame");

      if (rd->channel == SDPCM_EVENT_CHANNEL) {
        brcmf_rx_event(bus->sdiodev->drvr, std::move(*frame));
      } else {
        brcmf_rx_frame(bus->sdiodev->drvr, std::move(*frame), false);
      }
    }

    uint32_t next_len = rd->len_nxtfrm << 4;
    rd->len = static_cast<uint16_t>(next_len);
    rd->len_nxtfrm = 0;
    // Treat all packets as event if we don't know
    rd->channel = SDPCM_EVENT_CHANNEL;
  }

  if (bus->rxskip) {
    --rd->seq_num;
  }
  bus->rx_seq = rd->seq_num;

  return max_frames - rxleft;
}

void brcmf_sdio_wait_event_wakeup(struct brcmf_sdio* bus) {
  sync_completion_signal(&bus->ctrl_wait);
  return;
}

static zx_status_t brcmf_sdio_tx_frame_hdr_align(struct brcmf_sdio* bus,
                                                 wlan::drivers::components::Frame& frame,
                                                 uint16_t* head_pad_out) {
  uint8_t* data = frame.Data();

  uintptr_t head_pad = reinterpret_cast<uintptr_t>(data) % bus->head_align;
  if (head_pad > 0) {
    frame.GrowHead(head_pad);
    data = frame.Data();
  }

  memset(frame.Data(), 0, head_pad + bus->tx_hdrlen);
  *head_pad_out = static_cast<uint16_t>(head_pad);

  return ZX_OK;
}

static uint16_t brcmf_sdio_compute_tail_pad(struct brcmf_sdio* bus, uint32_t frame_size,
                                            bool last_frame, uint16_t total_size) {
  uint32_t alignment = last_frame ? bus->sdiodev->func2->blocksize : bus->sgentry_align;
  // For individual frames we align the frame size, for the last frame we align the entire chain
  uint32_t size = last_frame ? total_size : frame_size;
  return brcmf_sdio_align_pad(size, alignment);
}

static zx_status_t brcmf_sdio_tx_frames_prep(struct brcmf_sdio* bus,
                                             cpp20::span<wlan::drivers::components::Frame>& frames,
                                             uint8_t channel) {
  TRACE_DURATION("brcmfmac:isr", "sdio_tx_frames_prep");

  uint16_t total_size = 0;
  uint8_t tx_seq = bus->tx_seq;
  for (auto frameIt = frames.begin(); frameIt != frames.end(); ++frameIt) {
    auto& frame = *frameIt;
    uint16_t head_pad = 0;
    zx_status_t err = brcmf_sdio_tx_frame_hdr_align(bus, frame, &head_pad);
    if (err != ZX_OK) {
      return err;
    }
    if (head_pad > 0) {
      memset(frame.Data() + head_pad, 0, head_pad);
    }
    total_size += frame.Size();

    uint8_t* data = frame.Data();
    uint32_t size = frame.Size();

    bool last_frame = frameIt + 1 == frames.end();

    uint16_t tail_pad = 0;
    if (bus->txglom) {
      tail_pad = brcmf_sdio_compute_tail_pad(bus, size, last_frame, total_size);
      if (tail_pad > 0) {
        total_size += tail_pad;
        frame.GrowTail(tail_pad);
      }
    }

    struct brcmf_sdio_hdrinfo hd_info = {
        .seq_num = tx_seq++,
        .channel = channel,
        .len = static_cast<uint16_t>(size),
        .dat_offset = static_cast<uint8_t>(bus->tx_hdrlen + head_pad),
        .lastfrm = last_frame,
        .tail_pad = tail_pad,
    };

    brcmf_sdio_hdpack(bus, data, &hd_info);
  }
  if (bus->txglom) {
    brcmf_sdio_update_hwhdr(frames.begin()->Data(), total_size);
  }

  return ZX_OK;
}

static zx_status_t brcmf_sdio_tx_frames(struct brcmf_sdio* bus,
                                        cpp20::span<wlan::drivers::components::Frame> frames) {
  zx_status_t err = brcmf_sdio_tx_frames_prep(bus, frames, SDPCM_DATA_CHANNEL);

  uint32_t frames_sent = 0;
  if (err == ZX_OK) {
    err = brcmf_sdiod_send_frames(bus->sdiodev, frames, &frames_sent);
    bus->sdcnt.f2txdata += frames_sent;

    if (err != ZX_OK) {
      brcmf_sdio_txfail(bus);
    }
  }

  static_assert(SDPCM_SEQ_WRAP == 256,
                "This code assumes that sequence numbers wrap around on 8 bits");
  static_assert(std::is_same<decltype(bus->tx_seq), uint8_t>::value,
                "This code assumes that bus->tx_seq is an uint8_t");
  bus->tx_seq += frames_sent;

  TRACE_DURATION("brcmfmac:isr", "sdio_tx_frames_complete");

  if (err != ZX_OK && frames_sent > 0) {
    // Some frames were sent successfully, complete those as a success, the rest will be completed
    // as failures below.
    brcmf_proto_bcdc_txcomplete(bus->sdiodev->drvr, frames.subspan(0, frames_sent), ZX_OK);
    frames = frames.subspan(frames_sent);
  }
  brcmf_proto_bcdc_txcomplete(bus->sdiodev->drvr, frames, err);

  return err;
}

// TODO(fxbug.dev/42151): Remove once bug resolved
static uint32_t brcmf_sdio_txq_full_errors = 0;
static bool brcmf_sdio_txq_full_debug_log = false;

static zx_status_t brcmf_sdio_send_tx_queue(struct brcmf_sdio* bus, uint32_t frame_count) {
  TRACE_DURATION("brcmfmac:isr", "send_tx_queue", "frame_count", frame_count);

  const uint8_t allowed_precedences = ~bus->flowcontrol;

  const uint32_t allowed = static_cast<uint8_t>(bus->tx_max - bus->tx_seq);
  const uint32_t num_frames = std::min(frame_count, allowed);
  // We shouldn't need to clear here but just to be safe.
  bus->tx_queue->tx_frames.clear();
  {
    std::lock_guard<std::mutex> lock(bus->tx_queue->txq_lock);
    bus->tx_queue->tx_queue.pop(num_frames, allowed_precedences, &bus->tx_queue->tx_frames);
  }
  if (bus->tx_queue->tx_frames.empty()) {
    return ZX_OK;
  }

  TRACE_DURATION("brcmfmac:isr", "sdio_tx_frames", "frame_count", TA_UINT32(num_frames), "allowed",
                 TA_UINT32(allowed));
  zx_status_t status = brcmf_sdio_tx_frames(bus, bus->tx_queue->tx_frames);
  bus->tx_queue->tx_frames.clear();
  return status;
}

static zx_status_t brcmf_sdio_tx_ctrlframe(struct brcmf_sdio* bus, uint8_t* frame, uint16_t len) {
  uint8_t doff;
  uint16_t pad;
  uint retries = 0;
  struct brcmf_sdio_hdrinfo hd_info = {};
  // TODO(cphoenix): ret, err, rv, error, status - more consistency is better.
  zx_status_t ret;

  TRACE_DURATION("brcmfmac:isr", "sdio_tx_ctrlframe");

  /* Back the pointer to make room for bus header */
  frame -= bus->tx_hdrlen;
  len += bus->tx_hdrlen;

  /* Add alignment padding (optional for ctl frames) */
  const auto doff_unsized = reinterpret_cast<uint64_t>(frame) % bus->head_align;
  if (doff_unsized > std::numeric_limits<uint8_t>::max()) {
    BRCMF_ERR("brcmf_sdio_tx_ctrlframe failed: doff invalid (overflow)");
    return ZX_ERR_INTERNAL;
  }
  doff = static_cast<uint8_t>(doff_unsized);
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
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(BYTES) && BRCMF_IS_ON(CTL), frame, len, "Tx Frame:");

  BRCMF_DBG_HEX_DUMP(!(BRCMF_IS_ON(BYTES) && BRCMF_IS_ON(CTL)) && BRCMF_IS_ON(HDRS), frame,
                     std::min<uint16_t>(len, 16), "TxHdr:");

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

static void brcmf_sdio_bus_stop(brcmf_bus* bus_if) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;
  struct brcmf_core* core = bus->sdio_core;
  uint32_t local_hostintmask;
  uint8_t saveclk;
  zx_status_t err;
  int thread_result;

  BRCMF_DBG(TRACE, "Enter");
  if (bus->watchdog_tsk) {
    bus->watchdog_should_stop.store(true);
    sync_completion_signal(&bus->watchdog_wait);
    BRCMF_DBG(TEMP, "Closing and joining SDIO watchdog task");
    thread_result = thrd_join(bus->watchdog_tsk, NULL);
    BRCMF_DBG(TEMP, "Result of thread join: %d", thread_result);
    bus->watchdog_tsk = 0;
  }

  if (sdiodev->state != BRCMF_SDIOD_NOMEDIUM) {
    sdio_claim_host(sdiodev->func1);
    /* Enable clock for device interrupts */
    brcmf_sdio_bus_sleep(bus, false, false);

    /* Disable and clear interrupts at the chip level also */
    brcmf_sdiod_func1_wl(sdiodev, core->base + SD_REG(hostintmask), 0, NULL);

    local_hostintmask = bus->hostintmask;
    bus->hostintmask = 0;

    /* Force backplane clocks to assure F2 interrupt propagates */
    saveclk = brcmf_sdiod_func1_rb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
    if (err == ZX_OK) {
      brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, (saveclk | SBSDIO_FORCE_HT), &err);
    }
    if (err != ZX_OK) {
      BRCMF_ERR("Failed to force clock for F2: err %s", zx_status_get_string(err));
    }

    /* Turn off the bus (F2), free any pending packets */
    BRCMF_DBG(INTR, "disable SDIO interrupts");
    sdio_disable_fn(&sdiodev->sdio_proto_fn2);

    /* Clear any pending interrupts now that F2 is disabled */
    brcmf_sdiod_func1_wl(sdiodev, core->base + SD_REG(intstatus), local_hostintmask, NULL);

    sdio_release_host(sdiodev->func1);
  }
  /* Clear the data packet queues */
  brcmu_pktq_flush(&bus->txq, true, NULL, NULL);

  /* Clear rx control and wake any waiters */
  // spin_lock_bh(&bus->rxctl_lock);
  sdiodev->drvr->irq_callback_lock.lock();
  bus->rx_ctl_frame.clear();
  // spin_unlock_bh(&bus->rxctl_lock);
  sdiodev->drvr->irq_callback_lock.unlock();
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

static zx_status_t brcmf_sdio_intr_rstatus(struct brcmf_sdio* bus) {
  struct brcmf_core* core = bus->sdio_core;
  uint32_t addr;
  uint32_t val;
  zx_status_t ret;

  addr = core->base + SD_REG(intstatus);

  val = brcmf_sdiod_func1_rl(bus->sdiodev, addr, &ret);
  bus->sdcnt.f1regdata++;
  if (ret != ZX_OK) {
    return ret;
  }

  val &= bus->hostintmask;
  bus->fcstate.store(!!(val & I_HMB_FC_STATE));

  /* Clear interrupts */
  if (val) {
    brcmf_sdiod_func1_wl(bus->sdiodev, addr, val, &ret);
    bus->sdcnt.f1regdata++;
    bus->intstatus.fetch_or(val);
  }

  return ret;
}

static bool brcmf_sdio_have_txq(struct brcmf_sdio* bus) {
  std::lock_guard lock(bus->tx_queue->txq_lock);
  return bus->tx_queue->tx_queue.size(~bus->flowcontrol) > 0;
}

static bool brcmf_sdio_dpc_has_more_work(struct brcmf_sdio* bus) {
  if (bus->intstatus.load()) {
    return true;
  }
  if (bus->ipend.load() > 0) {
    return true;
  }
  if (bus->fcstate.load()) {
    return false;
  }
  if (!data_ok(bus)) {
    return false;
  }
  if (brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol)) {
    return true;
  }
  return brcmf_sdio_have_txq(bus);
}

static void brcmf_sdio_dpc(struct brcmf_sdio* bus) {
  struct brcmf_sdio_dev* sdiod = bus->sdiodev;
  uint32_t newstatus = 0;
  uint32_t intstat_addr = bus->sdio_core->base + SD_REG(intstatus);
  uint32_t intstatus;
  const uint txlimit = bus->txbound; /* Tx frames to send before resched */
  zx_status_t err = ZX_OK;

  TRACE_DURATION("brcmfmac:isr", "dpc");

  sdio_claim_host(bus->sdiodev->func1);

  /* If waiting for HTAVAIL, check status */
  if (!bus->sr_enabled && bus->clkstate == CLK_PENDING) {
    uint8_t clkctl;
    uint8_t devctl = 0;

#if !defined(NDEBUG)
    /* Check for inconsistent device control */
    devctl = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
#endif /* !defined(NDEBUG) */

    /* Read CSR, if clock on switch to AVAIL, else ignore */
    clkctl = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);

    BRCMF_DBG(SDIO, "DPC: PENDING, devctl 0x%02x clkctl 0x%02x", devctl, clkctl);

    if (SBSDIO_HTAV(clkctl)) {
      devctl = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_DEVICE_CTL, &err);
      devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
      brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_DEVICE_CTL, devctl, &err);
      bus->clkstate = CLK_AVAIL;
    }
  }

  /* Make sure backplane clock is on */
  brcmf_sdio_bus_sleep(bus, false, true);

  /* Pending interrupt indicates new device status */
  if (bus->ipend.load() > 0) {
    bus->ipend.store(0);
    err = brcmf_sdio_intr_rstatus(bus);
  }

  /* Start with leftover status bits */
  intstatus = bus->intstatus.exchange(0);

  /* Handle flow-control change: read new state in case our ack
   * crossed another change interrupt.  If change still set, assume
   * FC ON for safety, let next loop through do the debounce.
   */
  if (intstatus & I_HMB_FC_CHANGE) {
    intstatus &= ~I_HMB_FC_CHANGE;
    brcmf_sdiod_func1_wl(sdiod, intstat_addr, I_HMB_FC_CHANGE, &err);

    newstatus = brcmf_sdiod_func1_rl(sdiod, intstat_addr, &err);

    bus->sdcnt.f1regdata += 2;
    bus->fcstate.store(!!(newstatus & (I_HMB_FC_STATE | I_HMB_FC_CHANGE)));
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
    BRCMF_ERR("Dongle reports WR_OOSYNC");
    intstatus &= ~I_WR_OOSYNC;
  }

  if (intstatus & I_RD_OOSYNC) {
    BRCMF_ERR("Dongle reports RD_OOSYNC");
    intstatus &= ~I_RD_OOSYNC;
  }

  if (intstatus & I_SBINT) {
    BRCMF_ERR("Dongle reports SBINT");
    intstatus &= ~I_SBINT;
  }

  /* Would be active due to wake-wlan in gSPI */
  if (intstatus & I_CHIPACTIVE) {
    BRCMF_DBG(SDIO, "Dongle reports CHIPACTIVE");
    intstatus &= ~I_CHIPACTIVE;
  }

  /* Ignore frame indications if rxskip is set */
  if (bus->rxskip) {
    intstatus &= ~I_HMB_FRAME_IND;
  }

  /* On frame indication, read available frames */
  if ((intstatus & I_HMB_FRAME_IND) && (bus->clkstate == CLK_AVAIL)) {
    brcmf_sdio_read_frames(bus, bus->rxbound);
    if (!bus->rxpending) {
      intstatus &= ~I_HMB_FRAME_IND;
    }
  }

  /* Keep still-pending events for next scheduling */
  if (intstatus) {
    bus->intstatus.fetch_or(intstatus);
  }

  brcmf_sdio_process_ctrl_tx(bus);

  /* Send queued frames (limit 1 if rx may still be pending) */
  if (bus->clkstate == CLK_AVAIL && !bus->fcstate.load() && txlimit > 0 && data_ok(bus)) {
    brcmf_sdio_send_tx_queue(bus, txlimit);
  } else if (unlikely(brcmf_sdio_txq_full_debug_log)) {
    // TODO(fxbug.dev/42151): Remove once bug resolved
    int len = brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol);
    if (len > 0) {
      BRCMF_INFO(
          "Not able to transmit queued frames right now, clkstate = %u, "
          "fcstate = %d, queue len = %d, txlimit = %u, data_ok = %s",
          bus->clkstate, bus->fcstate.load(), len, txlimit, data_ok(bus) ? "true" : "false");
      if (!data_ok(bus)) {
        BRCMF_INFO("The tx_window is not ready, tx_max: %d, tx_seq: %d", bus->tx_max, bus->tx_seq);
      }
    }
  }

  if ((bus->sdiodev->state != BRCMF_SDIOD_DATA) || (err != ZX_OK)) {
    BRCMF_ERR("failed backplane access over SDIO, halting operation");
    bus->intstatus.store(0);
    brcmf_sdio_if_ctrl_frame_stat_set(bus, [&bus]() {
      bus->ctrl_frame_err = ZX_ERR_IO_REFUSED;
      std::atomic_thread_fence(std::memory_order_seq_cst);
      brcmf_sdio_wait_event_wakeup(bus);
    });
  } else if (brcmf_sdio_dpc_has_more_work(bus)) {
    bus->dpc_triggered.store(true);
  }
}

static zx_status_t brcmf_sdio_bus_get_tx_depth(brcmf_bus* bus_if, uint16_t* out_len) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;

  std::lock_guard lock(bus->tx_queue->txq_lock);
  if (bus->tx_queue->tx_queue.capacity() > std::numeric_limits<uint16_t>::max()) {
    return ZX_ERR_INTERNAL;
  }
  *out_len = static_cast<uint16_t>(bus->tx_queue->tx_queue.capacity());
  return ZX_OK;
}

static zx_status_t brcmf_sdio_bus_get_rx_depth(brcmf_bus* bus_if, uint16_t* out_len) {
  *out_len = RXDEPTH;
  return ZX_OK;
}

static zx_status_t brcmf_sdio_get_tail_length(brcmf_bus* bus_if, uint16_t* tail_length_out) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;

  // Allow for block size padding at the end of frames
  *tail_length_out = sdiodev->func2->blocksize;
  return ZX_OK;
}

static zx_status_t brcmf_sdio_bus_flush_txq(brcmf_bus* bus_if, int ifidx) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;

  wlan::drivers::components::FrameContainer frames;
  std::lock_guard lock(bus->tx_queue->txq_lock);
  bus->tx_queue->tx_queue.pop_if(
      [&](const wlan::drivers::components::Frame& frame) { return frame.PortId() == ifidx; },
      &frames);

  brcmf_proto_bcdc_txcomplete(sdiodev->drvr, frames, ZX_ERR_CANCELED);

  return ZX_OK;
}

static zx_status_t brcmf_sdio_bus_flush_buffers(brcmf_bus* bus_if) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;

  // Prevent DPC from running while doing this, this ensures that we don't miss any buffers that are
  // currently in flight.
  sdio_claim_host(bus->sdiodev->func1);

  {
    // RX space is flushed by completing RX buffers with a size of zero.
    std::lock_guard lock(bus->rx_tx_data.rx_space);
    wlan::drivers::components::FrameContainer frames =
        bus->rx_tx_data.rx_space.Acquire(bus->rx_tx_data.rx_space.size());
    for (auto& frame : frames) {
      frame.SetSize(0);
    }
    brcmf_rx_frames(sdiodev->drvr, std::move(frames));
    bus->rx_tx_data.rx_space_valid = false;
  }
  {
    // TX buffers are flushed by completing them with a status of ZX_ERR_UNAVAILABLE.
    wlan::drivers::components::FrameContainer frames;
    std::lock_guard lock(bus->tx_queue->txq_lock);
    constexpr uint8_t kAllowAllPriorities = 0xFFu;
    bus->tx_queue->tx_queue.pop(bus->tx_queue->tx_queue.size(), kAllowAllPriorities, &frames);
    brcmf_proto_bcdc_txcomplete(sdiodev->drvr, frames, ZX_ERR_UNAVAILABLE);
  }

  sdio_release_host(bus->sdiodev->func1);

  return ZX_OK;
}

zx::status<uint8_t*> brcmf_map_vmo(brcmf_sdio* bus, uint8_t vmo_id, zx_handle_t vmo,
                                   uint64_t vmo_size) {
  if (vmo_id == kInternalVmoId && bus->rx_tx_data.vmo_addrs[vmo_id] != nullptr) {
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }

  zx_vaddr_t addr = 0;
  zx_status_t err = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                                vmo_size, &addr);
  if (err != ZX_OK) {
    BRCMF_ERR("Unable to map VMO: %s", zx_status_get_string(err));
    return zx::error(err);
  }
  {
    std::lock_guard<std::mutex> lock(bus->rx_tx_data.vmos_mutex);
    if (vmo_id >= std::size(bus->rx_tx_data.vmos)) {
      BRCMF_ERR("vmo_id %u out of range, max value is %lu", vmo_id,
                std::size(bus->rx_tx_data.vmos));
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    bus->rx_tx_data.vmos[vmo_id].id = vmo_id;
    bus->rx_tx_data.vmos[vmo_id].handle = vmo;
    bus->rx_tx_data.vmos[vmo_id].size = vmo_size;
  }

  return zx::success(reinterpret_cast<uint8_t*>(addr));
}

static zx_status_t brcmf_sdio_prepare_vmo(brcmf_bus* bus_if, uint8_t vmo_id, zx_handle_t vmo,
                                          uint8_t* mapped_addr, size_t mapped_size) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;

  zx_handle_t fn1_vmo = ZX_HANDLE_INVALID;
  zx_handle_t fn2_vmo = ZX_HANDLE_INVALID;

  sdiodev->bus->rx_tx_data.vmo_addrs[vmo_id] = mapped_addr;

  zx_status_t err = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &fn1_vmo);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to duplicate VMO handle for SDIO bus: %s", zx_status_get_string(err));
    return err;
  }

  err = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &fn2_vmo);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to duplicate VMO handle for SDIO bus: %s", zx_status_get_string(err));
    return err;
  }

  err = sdio_register_vmo(&sdiodev->sdio_proto_fn1, vmo_id, fn1_vmo, 0, mapped_size,
                          SDMMC_VMO_RIGHT_READ | SDMMC_VMO_RIGHT_WRITE);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to register VMO for func2: %s", zx_status_get_string(err));
    return err;
  }

  err = sdio_register_vmo(&sdiodev->sdio_proto_fn2, vmo_id, fn2_vmo, 0, mapped_size,
                          SDMMC_VMO_RIGHT_READ | SDMMC_VMO_RIGHT_WRITE);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to register VMO for func2: %s", zx_status_get_string(err));
    return err;
  }

  return ZX_OK;
}

static zx_status_t brcmf_sdio_release_vmo(brcmf_bus* bus_if, uint8_t vmo_id) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;

  std::lock_guard<std::mutex> vmos_lock(bus->rx_tx_data.vmos_mutex);
  if (vmo_id >= std::size(bus->rx_tx_data.vmos)) {
    BRCMF_ERR("vmo_id %u out of range, max value is %lu", vmo_id, std::size(bus->rx_tx_data.vmos));
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (bus->rx_tx_data.vmos[vmo_id].handle == ZX_HANDLE_INVALID) {
    BRCMF_ERR("Attempt to relase invalid VMO %u", vmo_id);
    return ZX_ERR_INVALID_ARGS;
  }

  bus->rx_tx_data.vmos[vmo_id].id = 0xFF;
  bus->rx_tx_data.vmos[vmo_id].handle = ZX_HANDLE_INVALID;
  bus->rx_tx_data.vmos[vmo_id].size = 0;
  bus->rx_tx_data.vmo_addrs[vmo_id] = nullptr;

  {
    // Remove all available RX space with a matching VMO id
    std::lock_guard lock(bus->rx_tx_data.rx_space);
    bus->rx_tx_data.rx_space.EraseFramesWithVmoId(vmo_id);
  }
  // Remove all frames in TX queue with a matching VMO id
  wlan::drivers::components::FrameContainer frames;
  {
    std::lock_guard lock(bus->tx_queue->txq_lock);
    bus->tx_queue->tx_queue.pop_if(
        [vmo_id](const wlan::drivers::components::Frame& frame) { return frame.VmoId() == vmo_id; },
        &frames);
  }
  brcmf_proto_bcdc_txcomplete(sdiodev->drvr, frames, ZX_ERR_CANCELED);

  // Unregister VMO for both f1 and f2
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  zx_status_t err = sdio_unregister_vmo(&sdiodev->sdio_proto_fn1, vmo_id, &vmo);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to release VMO %u for func1: %s", vmo_id, zx_status_get_string(err));
  }
  err = sdio_unregister_vmo(&sdiodev->sdio_proto_fn2, vmo_id, &vmo);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to release VMO %u for func2: %s", vmo_id, zx_status_get_string(err));
  }
  return err;
}

static zx_status_t brcmf_sdio_queue_rx_space(struct brcmf_bus* bus_if,
                                             const rx_space_buffer_t* buffers_list,
                                             size_t buffers_count, uint8_t* vmo_addrs[]) {
  struct brcmf_sdio* bus = bus_if->bus_priv.sdio->bus;
  std::lock_guard lock(bus->rx_tx_data.rx_space);
  bus->rx_tx_data.rx_space.Store(buffers_list, buffers_count, vmo_addrs);
  bus->rx_tx_data.rx_space_valid = true;
  return ZX_OK;
}

static inline wlan::drivers::components::FrameContainer brcmf_sdio_acquire_tx_space(
    struct brcmf_bus* bus, size_t count) {
  return brcmf_sdio_acquire_tx_space(bus->bus_priv.sdio->bus, count);
}

static uint32_t PriorityToPrecedence(uint8_t priority) {
  static constexpr uint32_t kLookup[] = {2, 1, 0, 3, 4, 5, 6, 7};
  return kLookup[priority];
}

static zx_status_t brcmf_sdio_bus_txframes(brcmf_bus* bus_if,
                                           cpp20::span<wlan::drivers::components::Frame> frames) {
  TRACE_DURATION("brcmfmac:sdio", "txframes");

  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;

  if (sdiodev->state != BRCMF_SDIOD_DATA) {
    brcmf_proto_bcdc_txcomplete(bus->sdiodev->drvr, frames, ZX_ERR_IO);
    return ZX_ERR_IO;
  }

  std::unique_ptr<std::vector<wlan::drivers::components::Frame>> failed_frames;
  {
    std::unique_ptr<wlan::drivers::components::Frame> dropped;
    std::lock_guard lock(bus->tx_queue->txq_lock);
    // Queue as many frames as we can
    for (auto& frame : frames) {
      frame.GrowHead(bus->tx_hdrlen);
      frame.SetPriority(PriorityToPrecedence(frame.Priority()));
      if (bus->tx_queue->tx_queue.push(std::move(frame), &dropped)) {
        ++bus->tx_queue->enqueue_count;
      }
      if (dropped) {
        if (!failed_frames) {
          // Defer allocation of this vector so that we only create it if there is a frame that
          // needs to be completed, either because it was evicted or because the frame we're trying
          // to push didn't fit. This should not be a common case so we don't want to perform this
          // step every time, only when it's  needed.
          failed_frames = std::make_unique<std::vector<wlan::drivers::components::Frame>>();
        }
        // Keep the evicted/failed frame for later, we want to complete them all in one batch.
        failed_frames->emplace_back(std::move(*dropped));
        failed_frames->back().ShrinkHead(bus->tx_hdrlen);

        // TODO(fxbug.dev/42151): Remove once bug resolved
        ++brcmf_sdio_txq_full_errors;
        if (brcmf_sdio_txq_full_errors >= 30 && !brcmf_sdio_txq_full_debug_log) {
          // We've seen a large number of these errors in a row, start providing
          // more debug information.
          BRCMF_WARN("Excessive out of bus->txq errors, enabling debug logging");
          brcmf_sdio_txq_full_debug_log = true;
        }
        sdiodev->drvr->device->GetInspect()->LogTxQueueFull();
        // Reset the pointer so that it's not used in the next loop iteration.
        dropped.reset();
        continue;
      }
      // TODO(fxbug.dev/42151): Remove once bug resolved
      // Reset the counter here in case there was just a spurious queue issue.
      // Also stop the debug logging so we don't spam the logs unnecessarily.
      brcmf_sdio_txq_full_errors = 0;
      brcmf_sdio_txq_full_debug_log = false;

#if !defined(NDEBUG)
      if (frame.Priority() < std::size(qcount) &&
          bus->tx_queue->tx_queue.size(frame.Priority()) > qcount[frame.Priority()]) {
        qcount[frame.Priority()] = bus->tx_queue->tx_queue.size(frame.Priority());
      }
#endif
    }
  }

  brcmf_sdio_trigger_dpc(bus);

  if (failed_frames) {
    // Some or all frames did not end up on the TX queue, complete them with an indication that
    // we could not process them. Don't do this while holding the TX queue lock in case additional
    // frames are queued up as part of completing frames.
    bus->sdcnt.tx_qfull += failed_frames->size();
    brcmf_proto_bcdc_txcomplete(bus->sdiodev->drvr, *failed_frames, ZX_ERR_NO_RESOURCES);
  }

  return ZX_OK;
}

#ifdef BRCMF_CONSOLE_LOG
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
    c->buf = static_cast<decltype(c->buf)>(malloc(c->bufsize));
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
      BRCMF_DBG(FWCON, "CONSOLE: %s", line);
    }
  }
break2:

  return ZX_OK;
}
#endif  // BRCMF_CONSOLE_LOG

zx_status_t brcmf_sdio_bus_txctl(brcmf_bus* bus_if, unsigned char* msg, uint msglen) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;
  zx_status_t wait_status;

  BRCMF_DBG(TRACE, "Enter");
  if (sdiodev->state != BRCMF_SDIOD_DATA) {
    return ZX_ERR_IO;
  }

  /* Send from dpc */
  sync_completion_reset(&bus->ctrl_wait);
  bus->ctrl_frame_buf = msg;
  if (msglen > std::numeric_limits<uint16_t>::max()) {
    BRCMF_ERR("brcmf_sdio_bus_txctl failed: msglen invalid (overflow)");
    return ZX_ERR_INTERNAL;
  }
  bus->ctrl_frame_len = static_cast<uint16_t>(msglen);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  bus->ctrl_frame_stat.store(true);
  brcmf_sdio_trigger_dpc(bus);

  // Wait for a response from firmware
  wait_status = sync_completion_wait(&bus->ctrl_wait, sdiodev->ctl_done_timeout);
  if (wait_status == ZX_ERR_TIMED_OUT) {
    BRCMF_ERR("timed out waiting for txctl sdio operation to complete: %s",
              zx_status_get_string(wait_status));
    brcmf_sdio_if_ctrl_frame_stat_clear(bus, []() {
      BRCMF_ERR(
          "Unexpected clearing of ctrl_frame_stat by brcmf_sdio_dpc thread even though sdio "
          "operation timed out.");
    });
    bus->sdcnt.tx_ctlerrs++;
    return wait_status;
  }

  zx_status_t status = ZX_OK;
  brcmf_sdio_if_ctrl_frame_stat_set(bus, [&status]() { status = ZX_ERR_SHOULD_WAIT; });

  if (status != ZX_OK) {
    BRCMF_ERR("txctl ctrl_frame timeout: %s", zx_status_get_string(status));
    bus->sdcnt.tx_ctlerrs++;
    return status;
  }

  BRCMF_DBG(SDIO, "ctrl_frame complete, err=%d", bus->ctrl_frame_err);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  bus->sdcnt.tx_ctlpkts++;
  return bus->ctrl_frame_err;
}

static zx_status_t brcmf_sdio_checkdied(struct brcmf_sdio* bus) {
  zx_status_t error;
  struct sdpcm_shared sh;

  error = brcmf_sdio_shared_read(bus, &sh);

  if (error != ZX_OK) {
    return error;
  }

  if ((sh.flags & SDPCM_SHARED_ASSERT_BUILT) == 0) {
    BRCMF_DBG(SDIO, "firmware not built with -assert");
  } else if (sh.flags & SDPCM_SHARED_ASSERT) {
    BRCMF_ERR("assertion in dongle");
  }

  if (sh.flags & SDPCM_SHARED_TRAP) {
    BRCMF_ERR("firmware trap in dongle");

    sh.flags &= (~SDPCM_SHARED_TRAP);
    // Clean up the share memory before triggering a recovery.
    error = brcmf_sdio_shared_write(bus, &sh);
    if (error != ZX_OK) {
      BRCMF_ERR("Write shared failed -- error: %s", zx_status_get_string(error));
      return error;
    }

    error = bus->sdiodev->drvr->recovery_trigger->firmware_crash_.Inc();
    if (error != ZX_OK) {
      BRCMF_ERR("Increase recovery trigger condition failed -- error: %s",
                zx_status_get_string(error));
      return error;
    }
  }

  return ZX_OK;
}

static zx_status_t brcmf_sdio_bus_rxctl(brcmf_bus* bus_if, unsigned char* msg, uint msglen,
                                        int* rxlen_out) {
  bool timeout;
  uint rxlen = 0;
  bool pending;
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;

  BRCMF_DBG(TRACE, "Enter");
  if (sdiodev->state != BRCMF_SDIOD_DATA) {
    return ZX_ERR_IO;
  }

  /* Wait until control frame is available */
  timeout = (brcmf_sdio_dcmd_resp_wait(bus, &pending) != ZX_OK);

  // spin_lock_bh(&bus->rxctl_lock);
  sdiodev->drvr->irq_callback_lock.lock();
  rxlen = bus->rx_ctl_frame.size();
  if (rxlen) {
    memcpy(msg, bus->rx_ctl_frame.data(), std::min(rxlen, msglen));
    bus->rx_ctl_frame.clear();
  }
  // spin_unlock_bh(&bus->rxctl_lock);
  sdiodev->drvr->irq_callback_lock.unlock();

  if (rxlen) {
    BRCMF_DBG(CTL, "resumed on rxctl frame, received %d, message length %d", rxlen, msglen);
  } else if (timeout) {
    BRCMF_ERR("resumed on timeout");
    brcmf_sdio_checkdied(bus);
  } else if (pending) {
    BRCMF_DBG(CTL, "cancelled");
    return ZX_ERR_UNAVAILABLE;
  } else {
    BRCMF_DBG(CTL, "resumed for unknown reason?");
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

static bool brcmf_sdio_verifymemory(struct brcmf_sdio_dev* sdiodev, uint32_t ram_addr,
                                    const void* ram_data, size_t ram_sz) {
  char* ram_cmp;
  zx_status_t err;
  bool ret = true;
  int len;

  /* read back and verify */
  BRCMF_DBG(SDIO, "Compare RAM dl & ul at 0x%08x; size=%zu", ram_addr, ram_sz);
  ram_cmp = static_cast<decltype(ram_cmp)>(malloc(MEMBLOCK));
  /* do not proceed while no memory but  */
  if (!ram_cmp) {
    return true;
  }

  const char* expected_data = static_cast<const char*>(ram_data);
  int address = ram_addr;
  int offset = 0;

  if (ram_sz > std::numeric_limits<int>::max()) {
    BRCMF_ERR("brcmf_sdio_verifymemory failed: ram_sz invalid (overflow)");
    return false;
  }
  const auto ram_sz_resized = static_cast<int>(ram_sz);
  while (offset < ram_sz_resized) {
    len = ((offset + MEMBLOCK) < ram_sz_resized) ? MEMBLOCK : ram_sz_resized - offset;
    err = brcmf_sdiod_ramrw(sdiodev, false, address, (uint8_t*)ram_cmp, len);
    if (err != ZX_OK) {
      BRCMF_ERR("error %d on reading %d membytes at 0x%08x", err, len, address);
      ret = false;
      break;
    } else if (memcmp(ram_cmp, expected_data + offset, len)) {
      /* On failure, find the byte offset so we can print a detailed error */
      for (int ndx = 0; ndx < len; ndx++) {
        if (ram_cmp[ndx] != expected_data[offset + ndx]) {
          BRCMF_ERR("Downloaded RAM image is corrupted at offset %d of %zu (saw:%#x expect:%#x)",
                    offset, ram_sz, ram_cmp[ndx], expected_data[offset + ndx]);
          break;
        }
      }
      ret = false;
      break;
    }
    offset += len;
    address += len;
  }

  free(ram_cmp);

  return ret;
}

static zx_status_t brcmf_sdio_download_code_file(struct brcmf_sdio* bus, const void* firmware,
                                                 size_t firmware_size) {
  zx_status_t err;

  BRCMF_DBG(TRACE, "Enter");

  err = brcmf_sdiod_ramrw(bus->sdiodev, true, bus->ci->rambase, const_cast<void*>(firmware),
                          firmware_size);
  if (err != ZX_OK)
    BRCMF_ERR("error %d on writing %zu membytes at 0x%08x", err, firmware_size, bus->ci->rambase);
  else if (!brcmf_sdio_verifymemory(bus->sdiodev, bus->ci->rambase, firmware, firmware_size)) {
    err = ZX_ERR_IO;
  }

  return err;
}

static zx_status_t brcmf_sdio_download_nvram(struct brcmf_sdio* bus, const void* vars,
                                             uint32_t varsz) {
  int address;
  zx_status_t err;

  BRCMF_DBG(TRACE, "Enter");

  address = bus->ci->ramsize - varsz + bus->ci->rambase;
  err = brcmf_sdiod_ramrw(bus->sdiodev, true, address, const_cast<void*>(vars), varsz);
  if (err != ZX_OK) {
    BRCMF_ERR("error %d on writing %d nvram bytes at 0x%08x", err, varsz, address);
  } else if (!brcmf_sdio_verifymemory(bus->sdiodev, address, vars, varsz)) {
    err = ZX_ERR_IO;
  }

  return err;
}

static zx_status_t brcmf_sdio_download_firmware(struct brcmf_sdio* bus, const void* firmware,
                                                size_t firmware_size, const void* nvram,
                                                size_t nvram_size) {
  zx_status_t bcmerror = ZX_OK;
  uint32_t rstvec;
  uint8_t attempt_times = 0;
  if (nvram_size > std::numeric_limits<uint32_t>::max()) {
    BRCMF_ERR("brcmf_sdio_download_firmware failed: nvram_size invalid (overflow)");
    return ZX_ERR_INTERNAL;
  }
  const auto nvram_size_resized = static_cast<uint32_t>(nvram_size);

  sdio_claim_host(bus->sdiodev->func1);
  brcmf_sdio_clkctl(bus, CLK_AVAIL, false);

  rstvec = *(const uint32_t*)firmware;
  BRCMF_DBG(SDIO, "firmware rstvec: %x", rstvec);
  // Download firmware with retries.
  do {
    attempt_times++;
    bcmerror = brcmf_sdio_download_code_file(bus, firmware, firmware_size);
    if (bcmerror == ZX_OK)
      break;
    BRCMF_ERR("firmware file download failed, %u retry attempts remaining.",
              FILE_LOAD_MAX_ATTEMPTS - attempt_times);
  } while (attempt_times < FILE_LOAD_MAX_ATTEMPTS);

  BRCMF_DBG(SDIO, "attempted %u times.", attempt_times);
  if (bcmerror != ZX_OK)
    goto err;

  attempt_times = 0;
  // Download nvram with retries.
  do {
    attempt_times++;
    bcmerror = brcmf_sdio_download_nvram(bus, nvram, nvram_size_resized);
    if (bcmerror == ZX_OK)
      break;
    BRCMF_ERR("nvram file download failed, %u retry attempts remaining.",
              FILE_LOAD_MAX_ATTEMPTS - attempt_times);
  } while (attempt_times < FILE_LOAD_MAX_ATTEMPTS);

  BRCMF_DBG(SDIO, "attempted %u times.", attempt_times);
  if (bcmerror != ZX_OK)
    goto err;

  /* Take arm out of reset */
  if (!brcmf_chip_set_active(bus->ci, rstvec)) {
    BRCMF_ERR("error getting out of ARM core reset");
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

  BRCMF_DBG(TRACE, "Enter");

  val = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_WAKEUPCTRL, &err);
  if (err != ZX_OK) {
    BRCMF_ERR("error reading SBSDIO_FUNC1_WAKEUPCTRL");
    return;
  }

  val |= 1 << SBSDIO_FUNC1_WCTRL_HTWAIT_SHIFT;
  brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_WAKEUPCTRL, val, &err);
  if (err != ZX_OK) {
    BRCMF_ERR("error writing SBSDIO_FUNC1_WAKEUPCTRL");
    return;
  }

  /* Add CMD14 Support */
  brcmf_sdiod_vendor_control_wb(
      bus->sdiodev, SDIO_CCCR_BRCM_CARDCAP,
      (SDIO_CCCR_BRCM_CARDCAP_CMD14_SUPPORT | SDIO_CCCR_BRCM_CARDCAP_CMD14_EXT), &err);
  if (err != ZX_OK) {
    BRCMF_ERR("error writing SDIO_CCCR_BRCM_CARDCAP");
    return;
  }

  brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_FORCE_HT, &err);
  if (err != ZX_OK) {
    BRCMF_ERR("error writing SBSDIO_FUNC1_CHIPCLKCSR");
    return;
  }

  /* set flag */
  bus->sr_enabled = true;
  BRCMF_DBG(SDIO, "SR enabled");
}

/* enable KSO bit */
static zx_status_t brcmf_sdio_kso_init(struct brcmf_sdio* bus) {
  struct brcmf_core* core = bus->sdio_core;
  uint8_t val;
  zx_status_t err = ZX_OK;

  BRCMF_DBG(TRACE, "Enter");

  /* KSO bit added in SDIO core rev 12 */
  if (core->rev < 12) {
    return ZX_OK;
  }

  val = brcmf_sdiod_func1_rb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, &err);
  if (err != ZX_OK) {
    BRCMF_ERR("error reading SBSDIO_FUNC1_SLEEPCSR");
    return err;
  }

  if (!(val & SBSDIO_FUNC1_SLEEPCSR_KSO_MASK)) {
    val |= (SBSDIO_FUNC1_SLEEPCSR_KSO_EN << SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT);
    brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR, val, &err);
    if (err != ZX_OK) {
      BRCMF_ERR("error writing SBSDIO_FUNC1_SLEEPCSR");
      return err;
    }
  }

  return ZX_OK;
}

static zx_status_t brcmf_sdio_bus_preinit(brcmf_bus* bus_if) {
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
    err = brcmf_iovar_data_set(sdiodev->drvr, "bus:txglom", &value, sizeof(uint32_t), nullptr);
  } else {
    /* otherwise, set txglomalign */
    value = sdiodev->settings->bus.sdio->sd_sgentry_align;
    /* SDIO ADMA requires at least 32 bit alignment */
    value = std::max<uint32_t>(value, DMA_ALIGNMENT);
    err = brcmf_iovar_data_set(sdiodev->drvr, "bus:txglomalign", &value, sizeof(uint32_t), nullptr);

    if (err == ZX_OK) {
      value = 20;
      err = brcmf_iovar_data_set(sdiodev->drvr, "bus:maxtxpktglom", &value, sizeof(value), nullptr);
      if (err != ZX_OK) {
        BRCMF_ERR("Failed to set max rxglom packets: %s", zx_status_get_string(err));
      }
    } else {
      BRCMF_ERR("Failed to set rxglom alignment to %u", value);
    }
  }

  if (err != ZX_OK) {
    goto done;
  }

  bus->tx_hdrlen = SDPCM_HWHDR_LEN + SDPCM_SWHDR_LEN;

  bus->txglom = false;
  value = 1;

  // This command is from the firmware's perspective, rx in firmware is tx from
  // our point of view. Hence the rxglom here, it's intended.
  err = brcmf_iovar_data_set(sdiodev->drvr, "bus:rxglom", &value, sizeof(value), nullptr);
  if (err != ZX_OK) {
    /* bus:rxglom is allowed to fail */
    BRCMF_ERR("Failed to enable tx glomming: %s\n", zx_status_get_string(err));
    err = ZX_OK;
  } else {
    bus->txglom = sdiodev->txglom = true;
    bus->tx_hdrlen += SDPCM_HWEXT_LEN;
  }

  brcmf_bus_add_txhdrlen(sdiodev->drvr, bus->tx_hdrlen);
  bus_if->always_use_fws_queue = false;

done:
  return err;
}

void brcmf_sdio_trigger_dpc(struct brcmf_sdio* bus) {
  if (!bus->dpc_triggered.load()) {
    bus->dpc_triggered.store(true);
    bus->brcmf_wq->Schedule(&bus->datawork);
  }
}

void brcmf_sdio_isr(struct brcmf_sdio* bus) {
  BRCMF_DBG(TRACE, "Enter");

  if (!bus) {
    BRCMF_ERR("bus is null pointer, exiting");
    return;
  }

  /* Count the interrupt call */
  bus->sdcnt.intrcount++;
  if (brcmf_sdio_intr_rstatus(bus) != ZX_OK) {
    BRCMF_ERR("failed backplane access");
  }

  /* Disable additional interrupts (is this needed now)? */
  if (!bus->intr) {
    BRCMF_ERR("isr w/o interrupt configured!");
  }

  bus->dpc_triggered.store(true);
  bus->brcmf_wq->Schedule(&bus->datawork);
}

void brcmf_sdio_event_handler(struct brcmf_sdio* bus);

static void brcmf_sdio_bus_watchdog(struct brcmf_sdio* bus) {
  BRCMF_DBG(TIMER, "Enter");

  /* Poll period: check device if appropriate. */
  if (!bus->sr_enabled && bus->poll && (++bus->polltick >= bus->pollrate)) {
    bool intstatus = false;

    /* Reset poll tick */
    bus->polltick = 0;

    /* Check device if no interrupts */
    if (!bus->intr || (bus->sdcnt.intrcount == bus->sdcnt.lastintrs)) {
      if (!bus->dpc_triggered.load()) {
        bool func1_pend;
        bool func2_pend;

        sdio_claim_host(bus->sdiodev->func1);

        sdio_intr_pending(&bus->sdiodev->sdio_proto_fn1, &func1_pend);
        sdio_intr_pending(&bus->sdiodev->sdio_proto_fn2, &func2_pend);
        sdio_release_host(bus->sdiodev->func1);
        intstatus = func1_pend || func2_pend;
      }

      /* If there is something, make like the ISR and
               schedule the DPC. */
      if (intstatus) {
        bus->sdcnt.pollcnt++;
        bus->ipend.store(1);

        bus->dpc_triggered.store(true);
        bus->brcmf_wq->Schedule(&bus->datawork);
      }
    }

    /* Update interrupt tracking */
    bus->sdcnt.lastintrs = bus->sdcnt.intrcount;
  }
#ifdef BRCMF_CONSOLE_LOG
  /* Poll for console output periodically */
  // This was the original check. But for some reason sdiodev->state never gets set to
  // BRCMF_SDIOD_DATA. Need to investiage TODO(fxbug.dev/36618)
  // (bus->sdiodev->state == BRCMF_SDIOD_DATA && BRCMF_IS_ON(FWCON) && bus->console_interval != 0)
  if (BRCMF_IS_ON(FWCON) && bus->console_interval != 0) {
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
#endif  // BRCMF_CONSOLE_LOG

// TODO(cphoenix): Turn "idle" back on once things are working, and see if anything breaks.
#ifdef TEMP_DISABLE_DO_IDLE
  /* On idle timeout clear activity flag and/or turn off clock */
  if (!bus->dpc_triggered.load()) {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if ((!bus->dpc_running) && (bus->idletime > 0) && (bus->clkstate == CLK_AVAIL)) {
      bus->idlecount++;
      if (bus->idlecount > bus->idletime) {
        BRCMF_DBG(SDIO, "idle");
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
#endif  // TEMP_DISABLE_DO_IDLE
}

void brcmf_sdio_event_handler(struct brcmf_sdio* bus) {
  bus->dpc_running = true;
  std::atomic_thread_fence(std::memory_order_seq_cst);
  while (bus->dpc_triggered.load()) {
    bus->dpc_triggered.store(false);
    brcmf_sdio_dpc(bus);
    bus->idlecount = 0;
  }
  bus->dpc_running = false;
}

static void brcmf_sdio_dataworker(WorkItem* work) {
  struct brcmf_sdio* bus = containerof(work, struct brcmf_sdio, datawork);
  // Lock here so that everything inside brcmf_sdio_event_handlers is protected.
  // This is to ensure that the event handler is only called from either the
  // workqueue or the interrupt thread. This is a little heavy-handed so there
  // is probably room for improvement here. Running without this lock will
  // eventually result in the driver failing though.
  sdio_claim_host(bus->sdiodev->func1);
  brcmf_sdio_event_handler(bus);
  sdio_release_host(bus->sdiodev->func1);
}

zx_status_t brcmf_sdio_load_files(brcmf_pub* drvr, bool reload) TA_NO_THREAD_SAFETY_ANALYSIS {
  zx_status_t status = ZX_OK;
  brcmf_bus* bus_if = drvr->bus_if;

  std::string firmware_binary;
  if ((status = wlan::brcmfmac::GetFirmwareBinary(
           drvr->device, brcmf_bus_type::BRCMF_BUS_TYPE_SDIO,
           static_cast<wlan::brcmfmac::CommonCoreId>(bus_if->chip), bus_if->chiprev,
           &firmware_binary)) != ZX_OK) {
    BRCMF_ERR("Load firmware binary failed, error: %s", zx_status_get_string(status));
    if (reload)
      drvr->fw_reloading.unlock();
    return status;
  }

  const size_t padded_size_firmware = ZX_ROUNDUP(firmware_binary.size(), SDIOD_SIZE_ALIGNMENT);
  firmware_binary.resize(padded_size_firmware, '\0');

  std::string nvram_binary;
  if ((status =
           wlan::brcmfmac::GetNvramBinary(drvr->device, brcmf_bus_type::BRCMF_BUS_TYPE_SDIO,
                                          static_cast<wlan::brcmfmac::CommonCoreId>(bus_if->chip),
                                          bus_if->chiprev, &nvram_binary)) != ZX_OK) {
    BRCMF_ERR("Load nvram binary failed, error: %s", zx_status_get_string(status));
    if (reload)
      drvr->fw_reloading.unlock();
    return status;
  }

  const size_t padded_size_nvram = ZX_ROUNDUP(nvram_binary.size(), SDIOD_SIZE_ALIGNMENT);
  nvram_binary.resize(padded_size_nvram, '\0');

  if (firmware_binary.size() > std::numeric_limits<uint32_t>::max()) {
    BRCMF_ERR("Firmware binary size too large");
    if (reload)
      drvr->fw_reloading.unlock();
    return ZX_ERR_INTERNAL;
  }

  if ((status = brcmf_sdio_firmware_callback(drvr, firmware_binary.data(), firmware_binary.size(),
                                             nvram_binary.data(), nvram_binary.size())) != ZX_OK) {
    BRCMF_ERR("Load nvram binary failed, error: %s", zx_status_get_string(status));
    if (reload)
      drvr->fw_reloading.unlock();
    return status;
  }

  std::string clm_binary;
  if ((status =
           wlan::brcmfmac::GetClmBinary(drvr->device, brcmf_bus_type::BRCMF_BUS_TYPE_SDIO,
                                        static_cast<wlan::brcmfmac::CommonCoreId>(bus_if->chip),
                                        bus_if->chiprev, &clm_binary)) != ZX_OK) {
    BRCMF_ERR("Load CLM binary failed, error: %s", zx_status_get_string(status));
    if (reload)
      drvr->fw_reloading.unlock();
    return status;
  }

  // Unlock firmware reload lock after reloading the firmware or in other failure branches above if
  // it's a reload.
  if (reload)
    drvr->fw_reloading.unlock();

  // The firmware IOVAR accesses to upload the CLM blob are always on ifidx 0, so we stub out an
  // appropriate brcmf_if instance here.
  brcmf_if ifp = {};
  ifp.drvr = drvr;
  ifp.ifidx = 0;
  if ((status = brcmf_c_process_clm_blob(&ifp, clm_binary)) != ZX_OK) {
    BRCMF_ERR("Process clm blob fail.");
    return status;
  }

  return ZX_OK;
}

zx_status_t brcmf_sdio_recovery(struct brcmf_bus* bus) TA_NO_THREAD_SAFETY_ANALYSIS {
  struct brcmf_sdio_dev* sdiod = bus->bus_priv.sdio;
  struct brcmf_pub* drvr = sdiod->drvr;
  zx_status_t error = ZX_OK;

  // Lock the firmware reload mutex for this function so that no interrupt will be handled in
  // the middle of it.
  drvr->fw_reloading.lock();
  // Close sdiod, so that no more data operation can proceed during during firmware reload.
  brcmf_sdiod_change_state(sdiod, BRCMF_SDIOD_DOWN);
  // Sdio clean-ups
  brcmf_sdio_reset(sdiod->bus);

  if ((error = brcmf_sdio_load_files(drvr, true)) != ZX_OK) {
    BRCMF_ERR("Failed to reload images - error: %s", zx_status_get_string(error));
    brcmf_proto_bcdc_detach(drvr);
    return error;
  }

  if ((error = brcmf_bus_started(sdiod->drvr, true)) != ZX_OK) {
    BRCMF_ERR("Initialization after bus started failed.");
    brcmf_proto_bcdc_detach(drvr);
    return error;
  }

  return ZX_OK;
}

void brcmf_sdio_log_stats(struct brcmf_bus* bus_if) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;

  zxlogf(INFO,
         "SDIO bus stats: FC: %x FC_ChangeCnt: %u TxSeq: %u TxMax: %u TxCtlCnt: %lu TxCtlErr: %lu,"
         " RxCtlCnt: %lu, RxCtlErr: %lu, RxOutOfBufs: %u, Intrs: %u, HdrRead: %u, PktReads: %u, "
         "PktWrites: %u",
         bus->flowcontrol, bus->sdcnt.fc_rcvd, bus->tx_seq, bus->tx_max, bus->sdcnt.tx_ctlpkts,
         bus->sdcnt.tx_ctlerrs, bus->sdcnt.rx_ctlpkts, bus->sdcnt.rx_ctlerrs,
         bus->sdcnt.rx_outofbufs, bus->sdcnt.intrcount, bus->sdcnt.f2rxhdrs, bus->sdcnt.f2rxdata,
         bus->sdcnt.f2txdata);
  std::lock_guard lock(bus->tx_queue->txq_lock);
  zxlogf(INFO,
         "SDIO txq stats: EnqueueCnt: %lu QFullCnt: %u QLen: %lu PerPrecLen [0]: %lu [1]: %lu [2]: "
         "%lu [3]: %lu",
         bus->tx_queue->enqueue_count, bus->sdcnt.tx_qfull, bus->tx_queue->tx_queue.size(),
         bus->tx_queue->tx_queue.size(1 << 0), bus->tx_queue->tx_queue.size(1 << 1),
         bus->tx_queue->tx_queue.size(1 << 2), bus->tx_queue->tx_queue.size(1 << 3));
}

int brcmf_sdio_oob_irqhandler(void* cookie) {
  struct brcmf_sdio_dev* sdiodev = static_cast<decltype(sdiodev)>(cookie);
  struct brcmf_sdio* bus = sdiodev->bus;
  zx_status_t status;
  uint32_t intstatus;

  while ((status = zx_interrupt_wait(sdiodev->irq_handle, NULL)) == ZX_OK) {
    bus->sdcnt.intrcount++;
    // Sleep the interrupt handling when reloading the firmware to reduce the chaos in driver caused
    // by queued interrupts.
    std::lock_guard<std::mutex> guard(sdiodev->drvr->fw_reloading);
    BRCMF_DBG_THROTTLE(INTR, "OOB intr triggered");
    sdio_claim_host(sdiodev->func1);
    if (brcmf_sdio_intr_rstatus(sdiodev->bus)) {
      BRCMF_ERR("failed backplane access");
    }
    intstatus = sdiodev->bus->intstatus.load();
    sdiodev->bus->dpc_triggered.store(true);
    brcmf_sdio_event_handler(sdiodev->bus);
    sdio_release_host(sdiodev->func1);
    if (intstatus == 0) {
      BRCMF_DBG_THROTTLE(TEMP, "Zero intstatus; pausing 5 msec");
      zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    }
    BRCMF_DBG_THROTTLE(INTR, "Done with OOB intr");
  }

  BRCMF_ERR("ISR exiting with status %s", zx_status_get_string(status));
  return (int)status;
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
      i = std::size(sdiod_drvstr_tab2_3v3) - 1;
      if (drivestrength >= sdiod_drvstr_tab2_3v3[i].strength) {
        str_tab = sdiod_drvstr_tab2_3v3;
        str_mask = 0x00000007;
        str_shift = 0;
      } else
        BRCMF_ERR("Invalid SDIO Drive strength for chip %s, strength=%d", ci->name, drivestrength);
      break;
    case SDIOD_DRVSTR_KEY(BRCM_CC_43362_CHIP_ID, 13):
      str_tab = sdiod_drive_strength_tab5_1v8;
      str_mask = 0x00003800;
      str_shift = 11;
      break;
    default:
      BRCMF_DBG(SDIO, "No SDIO driver strength init needed for chip %s rev %d pmurev %d", ci->name,
                ci->chiprev, ci->pmurev);
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
    brcmf_sdiod_func1_wl(sdiodev, addr, 1, NULL);
    cc_data_temp = brcmf_sdiod_func1_rl(sdiodev, addr, NULL);
    cc_data_temp &= ~str_mask;
    drivestrength_sel <<= str_shift;
    cc_data_temp |= drivestrength_sel;
    brcmf_sdiod_func1_wl(sdiodev, addr, cc_data_temp, NULL);

    BRCMF_DBG(SDIO, "SDIO: %d mA (req=%d mA) drive strength selected, set to 0x%08x",
              str_tab[i].strength, drivestrength, cc_data_temp);
  }
}

static zx_status_t brcmf_sdio_buscoreprep(void* ctx) {
  struct brcmf_sdio_dev* sdiodev = static_cast<decltype(sdiodev)>(ctx);
  zx_status_t err = ZX_OK;
  uint8_t clkval, clkset;

  /* Try forcing SDIO core to do ALPAvail request only */
  clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ;
  brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
  if (err != ZX_OK) {
    BRCMF_ERR("error writing for HT off");
    return err;
  }

  /* If register supported, wait for ALPAvail and then force ALP */
  /* This may take up to 15 milliseconds */
  clkval = brcmf_sdiod_func1_rb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, NULL);

  if ((clkval & ~SBSDIO_AVBITS) != clkset) {
    BRCMF_ERR("ChipClkCSR access: wrote 0x%02x read 0x%02x", clkset, clkval);
    return ZX_ERR_IO_REFUSED;
  }

  SPINWAIT(((clkval = brcmf_sdiod_func1_rb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, NULL)),
            !SBSDIO_ALPAV(clkval)),
           PMU_MAX_TRANSITION_DLY_USEC);

  if (!SBSDIO_ALPAV(clkval)) {
    BRCMF_ERR("timeout on ALPAV wait, clkval 0x%02x", clkval);
    return ZX_ERR_SHOULD_WAIT;
  }

  clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP;
  brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
  zx_nanosleep(zx_deadline_after(ZX_USEC(65)));

  /* Also, disable the extra SDIO pull-ups */
  brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_SDIOPULLUP, 0, NULL);

  return ZX_OK;
}

static void brcmf_sdio_buscore_activate(void* ctx, struct brcmf_chip* chip, uint32_t rstvec) {
  struct brcmf_sdio_dev* sdiodev = static_cast<decltype(sdiodev)>(ctx);
  struct brcmf_core* core = sdiodev->bus->sdio_core;
  uint32_t reg_addr;

  /* clear all interrupts */
  reg_addr = core->base + SD_REG(intstatus);
  brcmf_sdiod_func1_wl(sdiodev, reg_addr, 0xFFFFFFFF, NULL);

  if (rstvec) { /* Write reset vector to address 0 */
    brcmf_sdiod_ramrw(sdiodev, true, 0, (void*)&rstvec, sizeof(rstvec));
  }
}

static uint32_t brcmf_sdio_buscore_read32(void* ctx, uint32_t addr) {
  struct brcmf_sdio_dev* sdiodev = static_cast<decltype(sdiodev)>(ctx);
  uint32_t val, rev;

  zx_status_t status;
  val = brcmf_sdiod_func1_rl(sdiodev, addr, &status);
  if (status != ZX_OK) {
    BRCMF_ERR("Failed to read 32 bits: %s", zx_status_get_string(status));
  }

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
  struct brcmf_sdio_dev* sdiodev = static_cast<decltype(sdiodev)>(ctx);

  brcmf_sdiod_func1_wl(sdiodev, addr, val, NULL);
}

static const struct brcmf_buscore_ops brcmf_sdio_buscore_ops = {
    .read32 = brcmf_sdio_buscore_read32,
    .write32 = brcmf_sdio_buscore_write32,
    .prepare = brcmf_sdio_buscoreprep,
    .activate = brcmf_sdio_buscore_activate,
};

static zx_status_t brcmf_sdio_probe_attach(struct brcmf_sdio* bus) {
  struct brcmf_sdio_dev* sdiodev;
  uint8_t clkctl = 0;
  zx_status_t err = ZX_OK;
  int reg_addr;
  uint16_t block_size;
  uint32_t reg_val;
  uint32_t drivestrength;
  brcmf_mp_device* settings = nullptr;
  brcmf_sdio_pd* sdio_settings = nullptr;

  sdiodev = bus->sdiodev;
  sdio_claim_host(sdiodev->func1);

  BRCMF_DBG(SDIO, "brcmfmac: F1 signature read @0x18000000=0x%4x",
            brcmf_sdiod_func1_rl(sdiodev, SI_ENUM_BASE, NULL));
  BRCMF_DBG(TEMP, "Survived signature read");

  /*
   * Force PLL off until brcmf_chip_attach()
   * programs PLL control regs
   */

  brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, BRCMF_INIT_CLKCTL1, &err);
  if (err == ZX_OK) {
    clkctl = brcmf_sdiod_func1_rb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
  }

  if (err != ZX_OK || ((clkctl & ~SBSDIO_AVBITS) != BRCMF_INIT_CLKCTL1)) {
    BRCMF_ERR("ChipClkCSR access: err %s wrote 0x%02x read 0x%02x", zx_status_get_string(err),
              BRCMF_INIT_CLKCTL1, clkctl);
    goto fail;
  }

  err = brcmf_chip_attach(sdiodev, &brcmf_sdio_buscore_ops, &bus->ci);
  if (err != ZX_OK) {
    BRCMF_ERR("brcmf_chip_attach failed: %s", zx_status_get_string(err));
    bus->ci = NULL;
    goto fail;
  }

  /* Pick up the SDIO core info struct from chip.c */
  bus->sdio_core = brcmf_chip_get_core(bus->ci, CHIPSET_SDIO_DEV_CORE);
  if (!bus->sdio_core) {
    BRCMF_ERR("call to brcmf_chip_get_core (SDIO_DEV_CORE) failed");
    err = ZX_ERR_INTERNAL;
    goto fail;
  }

  /* Pick up the CHIPCOMMON core info struct, for bulk IO in bcmsdh.c */
  sdiodev->cc_core = brcmf_chip_get_core(bus->ci, CHIPSET_CHIPCOMMON_CORE);
  if (!sdiodev->cc_core) {
    BRCMF_ERR("call to brcmf_chip_get_core (CHIPCOMMON_CORE) failed");
    err = ZX_ERR_INTERNAL;
    goto fail;
  }

  settings = static_cast<decltype(settings)>(calloc(1, sizeof(*settings)));
  if (!settings) {
    BRCMF_ERR("failed to allocate device parameters");
    err = ZX_ERR_NO_MEMORY;
    goto fail;
  }
  brcmf_get_module_param(BRCMF_BUS_TYPE_SDIO, bus->ci->chip, bus->ci->chiprev, settings);
  sdiodev->settings = settings;

  sdio_settings = static_cast<decltype(sdio_settings)>(calloc(1, sizeof(*sdio_settings)));
  if (!sdio_settings) {
    BRCMF_ERR("failed to allocate SDIO parameters");
    err = ZX_ERR_NO_MEMORY;
    goto fail;
  }

  err = sdio_get_block_size(&sdiodev->sdio_proto_fn2, &block_size);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to get block size: %s", zx_status_get_string(err));
    goto fail;
  }
  BRCMF_INFO("Setting s/g entry alignment to block size %u", block_size);

  // TODO(cphoenix): Do we really want to use default? (If so, delete =0 lines because calloc)
  sdio_settings->txglomsz = kMaxTxGlomFrames;
  sdio_settings->sd_sgentry_align = block_size;
  sdio_settings->sd_head_align = 0;         // Use default
  sdio_settings->drive_strength = 0;        // Use default
  sdio_settings->oob_irq_supported = true;  // TODO(cphoenix): Always?
  sdiodev->settings->bus.sdio = sdio_settings;

  /* platform specific configuration:
   *   alignments must be at least 4 bytes for ADMA
   */
  bus->head_align = DMA_ALIGNMENT;
  bus->sgentry_align = DMA_ALIGNMENT;
  if (sdiodev->settings->bus.sdio->sd_head_align > DMA_ALIGNMENT) {
    if (sdiodev->settings->bus.sdio->sd_head_align > std::numeric_limits<uint16_t>::max()) {
      BRCMF_ERR("brcmf_sdio_probe_attach error: sd_head_align invalid (overflow)");
      err = ZX_ERR_INTERNAL;
      goto fail;
    }
    bus->head_align = static_cast<uint16_t>(sdiodev->settings->bus.sdio->sd_head_align);
  }
  if (sdiodev->settings->bus.sdio->sd_sgentry_align > DMA_ALIGNMENT) {
    if (sdiodev->settings->bus.sdio->sd_sgentry_align > std::numeric_limits<uint16_t>::max()) {
      BRCMF_ERR("brcmf_sdio_probe_attach error: sgentry_align invalid (overflow)");
      err = ZX_ERR_INTERNAL;
      goto fail;
    }
    bus->sgentry_align = static_cast<uint16_t>(sdiodev->settings->bus.sdio->sd_sgentry_align);
  }

  sdiodev->txglom = bus->txglom;
  sdiodev->txglomsz = sdiodev->settings->bus.sdio->txglomsz;

  err = brcmf_sdio_kso_init(bus);
  if (err != ZX_OK) {
    BRCMF_ERR("error enabling KSO: %s", zx_status_get_string(err));
    goto fail;
  }

  if (sdiodev->settings->bus.sdio->drive_strength) {
    drivestrength = sdiodev->settings->bus.sdio->drive_strength;
  } else {
    drivestrength = DEFAULT_SDIO_DRIVE_STRENGTH;
  }
  brcmf_sdio_drivestrengthinit(sdiodev, bus->ci, drivestrength);

  /* Set card control so an SDIO card reset does a WLAN backplane reset */
  reg_val = brcmf_sdiod_vendor_control_rb(sdiodev, SDIO_CCCR_BRCM_CARDCTRL, &err);
  if (err != ZX_OK) {
    BRCMF_ERR("vendor_control_rb failed: %s", zx_status_get_string(err));
    goto fail;
  }

  reg_val |= SDIO_CCCR_BRCM_CARDCTRL_WLANRESET;
  if (reg_val > std::numeric_limits<uint8_t>::max()) {
    BRCMF_ERR("vendor_control_wb cannot be called: reg_val invalid (overflow)");
    err = ZX_ERR_INTERNAL;
    goto fail;
  }
  brcmf_sdiod_vendor_control_wb(sdiodev, SDIO_CCCR_BRCM_CARDCTRL, static_cast<uint8_t>(reg_val),
                                &err);
  if (err != ZX_OK) {
    BRCMF_ERR("vendor_control_wb failed: %s", zx_status_get_string(err));
    goto fail;
  }

  /* set PMUControl so a backplane reset does PMU state reload */
  reg_addr = CORE_CC_REG(brcmf_chip_get_pmu(bus->ci)->base, pmucontrol);
  reg_val = brcmf_sdiod_func1_rl(sdiodev, reg_addr, &err);
  if (err != ZX_OK) {
    BRCMF_ERR("func1_rl failed: %s", zx_status_get_string(err));
    goto fail;
  }

  reg_val |= (BC_CORE_POWER_CONTROL_RELOAD << BC_CORE_POWER_CONTROL_SHIFT);

  brcmf_sdiod_func1_wl(sdiodev, reg_addr, reg_val, &err);
  if (err != ZX_OK) {
    BRCMF_ERR("func1_wl failed: %s", zx_status_get_string(err));
    goto fail;
  }

  sdio_release_host(sdiodev->func1);

  brcmu_pktq_init(&bus->txq, (PRIOMASK + 1), TXQLEN);

  /* allocate header buffer */
  bus->hdrbuf = static_cast<decltype(bus->hdrbuf)>(calloc(1, MAX_HDR_READ + bus->head_align));
  if (!bus->hdrbuf) {
    BRCMF_ERR("failed to allocate memory for SDIO hdrbuf");
    // Don't go to 'fail' here because we've already released the host
    return ZX_ERR_NO_MEMORY;
  }
  /* Locate an appropriately-aligned portion of hdrbuf */
  bus->rxhdr = (uint8_t*)ZX_ROUNDUP((unsigned long)&bus->hdrbuf[0], bus->head_align);

  /* Set the poll and/or interrupt flags */
  bus->intr = true;
  bus->poll = false;
  if (bus->poll) {
    bus->pollrate = 1;
  }

  BRCMF_DBG(TEMP, "Exit");
  ZX_DEBUG_ASSERT(err == ZX_OK);
#ifdef BRCMF_CONSOLE_LOG
  if (BRCMF_IS_ON(FWCON)) {
    bus->console_interval = BRCMF_CONSOLE_INTERVAL;
  }
#endif  // BRCMF_CONSOLE_LOG
  return err;

fail:
  sdio_release_host(sdiodev->func1);
  BRCMF_DBG(TEMP, "* * FAIL");
  return err;
}

static int brcmf_sdio_watchdog_thread(void* data) {
  struct brcmf_sdio* bus = (struct brcmf_sdio*)data;

  /* Run until signal received */
  while (1) {
    if (bus->watchdog_should_stop.load()) {
      break;
    }
    // Wait for watchdog signal
    sync_completion_wait(&bus->watchdog_wait, ZX_TIME_INFINITE);

    if (bus->wd_active.load()) {
      brcmf_sdio_bus_watchdog(bus);
    }
    /* Count the tick for reference */
    bus->sdcnt.tickcnt++;
    sync_completion_reset(&bus->watchdog_wait);
  }
  return 0;
}

static void brcmf_sdio_watchdog(struct brcmf_sdio* bus) {
  bus->sdiodev->drvr->irq_callback_lock.lock();

  if (bus->watchdog_tsk) {
    // Signal watchdog
    sync_completion_signal(&bus->watchdog_wait);
    /* Reschedule the watchdog */
    if (bus->wd_active.load()) {
      bus->timer->Start(ZX_MSEC(BRCMF_WD_POLL_MSEC));
    }
  }
  bus->sdiodev->drvr->irq_callback_lock.unlock();
}

static zx_status_t brcmf_get_wifi_metadata(brcmf_bus* bus_if, void* data, size_t exp_size,
                                           size_t* actual) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  return sdiodev->drvr->device->DeviceGetMetadata(DEVICE_METADATA_WIFI_CONFIG, data, exp_size,
                                                  actual);
}

static zx_status_t brcmf_sdio_get_bootloader_macaddr(brcmf_bus* bus_if, uint8_t* mac_addr) {
  struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;
  static uint8_t bootloader_macaddr[ETH_ALEN];
  static bool memoized = false;
  zx_status_t status;

  if (!memoized) {
    status = brcmf_sdiod_get_bootloader_macaddr(sdiodev, bootloader_macaddr);
    if (status != ZX_OK) {
      return status;
    }
    memoized = true;
  }

  memcpy(mac_addr, bootloader_macaddr, sizeof(bootloader_macaddr));
  return ZX_OK;
}

static const struct brcmf_bus_ops brcmf_sdio_bus_ops = {
    .get_bus_type = []() { return BRCMF_BUS_TYPE_SDIO; },
    .get_bootloader_macaddr = brcmf_sdio_get_bootloader_macaddr,
    .get_wifi_metadata = brcmf_get_wifi_metadata,
    .preinit = brcmf_sdio_bus_preinit,
    .stop = brcmf_sdio_bus_stop,
    .txframes = brcmf_sdio_bus_txframes,
    .txctl = brcmf_sdio_bus_txctl,
    .rxctl = brcmf_sdio_bus_rxctl,
    .flush_txq = brcmf_sdio_bus_flush_txq,
    .flush_buffers = brcmf_sdio_bus_flush_buffers,
    .get_tx_depth = brcmf_sdio_bus_get_tx_depth,
    .get_rx_depth = brcmf_sdio_bus_get_rx_depth,
    .get_tail_length = brcmf_sdio_get_tail_length,
    .recovery = brcmf_sdio_recovery,
    .log_stats = brcmf_sdio_log_stats,
    .prepare_vmo = brcmf_sdio_prepare_vmo,
    .release_vmo = brcmf_sdio_release_vmo,
    .queue_rx_space = brcmf_sdio_queue_rx_space,
    .acquire_tx_space = brcmf_sdio_acquire_tx_space,
};

zx_status_t brcmf_sdio_firmware_callback(brcmf_pub* drvr, const void* firmware,
                                         size_t firmware_size, const void* nvram,
                                         size_t nvram_size) {
  zx_status_t err = ZX_OK;
  struct brcmf_sdio_dev* sdiodev = drvr->bus_if->bus_priv.sdio;
  struct brcmf_sdio* bus = sdiodev->bus;
  struct brcmf_sdio_dev* sdiod = bus->sdiodev;
  struct brcmf_core* core = bus->sdio_core;
  struct sdpcm_shared sh = {};
  uint8_t saveclk = 0;

  BRCMF_DBG(TRACE, "Enter:");

  if (err != ZX_OK) {
    goto fail;
  }

  /* try to download image and nvram to the dongle */
  bus->alp_only = true;
  err = brcmf_sdio_download_firmware(bus, firmware, firmware_size, nvram, nvram_size);
  if (err != ZX_OK) {
    goto fail;
  }
  bus->alp_only = false;

  /* Start the watchdog timer */
  bus->sdcnt.tickcnt = 0;
  // TODO(fxbug.dev/29365): This call apparently has no effect because the state isn't
  // BRCMF_SDIOD_DATA. This was in the original driver. Once interrupts are working, figure out
  // what's going on.
  brcmf_sdio_wd_timer(bus, true);

  // Magic 200 ms pause here, because 100ms worked in the hard-coded debug recipe.
  // In addition, Broadcom said that a pause after booting may be necessary.
  // The original Linux driver doesn't have it, but I don't recommend removing this
  // without LOTS of stress testing.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));
  sdio_claim_host(sdiodev->func1);

  /* Make sure backplane clock is on, needed to generate F2 interrupt */
  bus->clkstate = CLK_NONE;  // TODO(cphoenix): TEMP FOR DEBUG
  brcmf_sdio_clkctl(bus, CLK_AVAIL, false);
  if (bus->clkstate != CLK_AVAIL) {
    BRCMF_ERR("Bad clockstate %d, should be %d", bus->clkstate, CLK_AVAIL);
    err = ZX_ERR_INTERNAL;
    goto release;
  }

  /* Force clocks on backplane to be sure F2 interrupt propagates */
  saveclk = brcmf_sdiod_func1_rb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, &err);
  if (err == ZX_OK) {
    brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, (saveclk | SBSDIO_FORCE_HT), &err);
  }
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to force clock for F2: err %s", zx_status_get_string(err));
    goto release;
  }

  /* Enable function 2 (frame transfers) */
  brcmf_sdiod_func1_wl(sdiod, core->base + SD_REG(tosbmailboxdata),
                       SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT, NULL);

  err = sdio_enable_fn(&sdiodev->sdio_proto_fn2);
  BRCMF_DBG(SDIO, "enable F2: err=%d", err);

  /* If F2 successfully enabled, set core and enable interrupts */
  if (err == ZX_OK) {
    /* Set up the interrupt mask and enable interrupts */
    bus->hostintmask = HOSTINTMASK;
    brcmf_sdiod_func1_wl(sdiod, core->base + SD_REG(hostintmask), bus->hostintmask, NULL);

    brcmf_sdiod_func1_wb(sdiodev, SBSDIO_WATERMARK, 8, &err);
  } else {
    /* Disable F2 again */
    sdio_disable_fn(&sdiodev->sdio_proto_fn2);
    goto release;
  }

  if (brcmf_chip_sr_capable(bus->ci)) {
    BRCMF_DBG(TEMP, "About to sr_init() (after 100 msec pause)");
    PAUSE;
    PAUSE;
    brcmf_sdio_sr_init(bus);
    PAUSE;
    PAUSE;
    BRCMF_DBG(TEMP, "Did sr_init() (100 msec ago)");
  } else {
    /* Restore previous clock setting */
    brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, saveclk, &err);
  }

  err = brcmf_sdio_shared_read(bus, &sh);
  BRCMF_DBG(TEMP, "Read shared returned %d", err);

#ifdef BRCMF_CONSOLE_LOG
  bus->console_addr = sh.console_addr;
  BRCMF_DBG(TEMP, "console_addr 0x%x", bus->console_addr);
  brcmf_sdio_readconsole(bus);
  BRCMF_DBG(TEMP, "Should have seen readconsole output");
#endif  // BRCMF_CONSOLE_LOG

  if (err == ZX_OK) {
    /* Allow full data communication using DPC from now on. */
    brcmf_sdiod_change_state(sdiod, BRCMF_SDIOD_DATA);
    // TODO(fxbug.dev/29365): The next line was added to enable watchdog to take effect immediately,
    // since it currently handles all interrupt conditions. This may or may not make the
    // previous call to brcmf_sdio_wd_timer() unnecessary; that call apparently had no effect
    // because the state wasn't BRCMF_SDIOD_DATA yet. Once interrupts are working, revisit
    // and figure out this logic.
    brcmf_sdio_wd_timer(bus, true);

    // brcmf_sdiod_intr_register() creates the interrupt thread and enables the sdio interrupt
    // through sdio proto, so it can be skipped while doing crash recovery.
    if (bus->sdiodev->drvr->fw_reloading.try_lock()) {
      err = brcmf_sdiod_intr_register(sdiodev);
      if (err != ZX_OK) {
        BRCMF_ERR("intr register failed:%d", err);
      }
      bus->sdiodev->drvr->fw_reloading.unlock();
    }
  }

  /* If we didn't come up, turn off backplane clock */
  if (err != ZX_OK) {
    BRCMF_ERR("Err %d on register OOB IRQ", err);
    brcmf_sdio_clkctl(bus, CLK_NONE, false);
  }

  sdio_release_host(sdiodev->func1);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  return ZX_OK;

release:
  sdio_release_host(sdiodev->func1);

fail:
  BRCMF_DBG(TRACE, "failed: err=%d", err);
  BRCMF_ERR("Need to implement driver release logic (fxbug.dev/29508)");
  // TODO(fxbug.dev/29508)
  // device_release_driver(&sdiodev->func2->dev);
  // device_release_driver(dev);

  return err;
}

static void create_internal_frame_space(struct brcmf_sdio* bus,
                                        wlan::drivers::components::FrameStorage& storage,
                                        size_t vmo_offset) __TA_REQUIRES(storage) {
  // Half the space is used for RX or TX and each buffer inside the space has a fixed size
  rx_space_buffer_t buffers[kDmaInternalBufferSize / 2 / kInternalVmoBufferSize];

  for (size_t i = 0; i < std::size(buffers); ++i) {
    buffers[i].id = kInternalBufferId;
    buffers[i].region.vmo = kInternalVmoId;
    buffers[i].region.length = kInternalVmoBufferSize;
    buffers[i].region.offset = vmo_offset + i * kInternalVmoBufferSize;
  }

  storage.Store(buffers, std::size(buffers), bus->rx_tx_data.vmo_addrs.data());
}

static zx_status_t brcmf_create_internal_rx_tx_space(struct brcmf_sdio* bus) {
  zx_status_t ret = zx::vmo::create(kDmaInternalBufferSize, 0, &bus->rx_tx_data.internal_vmo);
  if (ret != ZX_OK) {
    BRCMF_ERR("Error creating internal VMO: %s", zx_status_get_string(ret));
    return ret;
  }

  zx::status<uint8_t*> address = brcmf_map_vmo(
      bus, kInternalVmoId, bus->rx_tx_data.internal_vmo.get(), kDmaInternalBufferSize);
  if (address.is_error()) {
    BRCMF_ERR("Failed to map internal VMO: %s", address.status_string());
    return ret;
  }

  ret =
      brcmf_sdio_prepare_vmo(bus->sdiodev->bus_if, kInternalVmoId,
                             bus->rx_tx_data.internal_vmo.get(), *address, kDmaInternalBufferSize);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to prepare internal VMO: %s", zx_status_get_string(ret));
    return ret;
  }

  {
    std::lock_guard lock(bus->rx_tx_data.rx_space_internal);
    create_internal_frame_space(bus, bus->rx_tx_data.rx_space_internal, 0);
  }
  {
    std::lock_guard lock(bus->rx_tx_data.tx_space);
    constexpr size_t tx_offset = kDmaInternalBufferSize / 2;
    create_internal_frame_space(bus, bus->rx_tx_data.tx_space, tx_offset);
  }

  return ZX_OK;
}

struct brcmf_sdio* brcmf_sdio_probe(struct brcmf_sdio_dev* sdiodev) {
  zx_status_t ret;
  int thread_result;
  struct brcmf_sdio* bus;
  WorkQueue* wq;
  std::optional<wlan::drivers::components::Frame> frame;

  BRCMF_DBG(TRACE, "Enter");
  /* Allocate private bus interface state */
  bus = new (std::nothrow) brcmf_sdio{};
  if (!bus) {
    goto fail;
  }

  bus->sdiodev = sdiodev;
  sdiodev->bus = bus;
  bus->txbound = BRCMF_TXBOUND;
  bus->rxbound = BRCMF_RXBOUND;
  bus->txminmax = BRCMF_TXMINMAX;
  bus->tx_seq = SDPCM_SEQ_WRAP - 1;

  bus->tx_queue = new brcmf_tx_queue(TXQLEN);

  bus->rx_glom = new brcmf_rx_glom();

  /* single-threaded workqueue */
  char name[WorkQueue::kWorkqueueNameMaxlen];
  static int queue_uniquify = 0;
  snprintf(name, WorkQueue::kWorkqueueNameMaxlen, "brcmf_wq/%d", queue_uniquify++);
  wq = new WorkQueue(name);
  if (!wq) {
    BRCMF_ERR("insufficient memory to create txworkqueue");
    goto fail;
  }
  bus->datawork = WorkItem(brcmf_sdio_dataworker);
  bus->brcmf_wq = wq;

  // Make sure we have RX/TX space before attaching so we can actually communicate over SDIO
  ret = brcmf_create_internal_rx_tx_space(bus);
  if (ret != ZX_OK) {
    BRCMF_ERR("Failed to create internal RX/TX space: %s", zx_status_get_string(ret));
    goto fail;
  }

  frame = brcmf_sdio_acquire_single_internal_rx_space(bus);
  if (!frame) {
    BRCMF_ERR("Failed to acquire RX frame");
    goto fail;
  }
  bus->rx_tx_data.rx_frame = std::move(*frame);

  frame = brcmf_sdio_acquire_single_tx_space(bus);
  if (!frame) {
    BRCMF_ERR("Failed to acquire TX frame");
    goto fail;
  }
  bus->rx_tx_data.tx_frame = std::move(*frame);

  /* attempt to attach to the dongle */
  ret = brcmf_sdio_probe_attach(bus);
  if (ret != ZX_OK) {
    BRCMF_ERR("brcmf_sdio_probe_attach failed: %s", zx_status_get_string(ret));
    goto fail;
  }

  // spin_lock_init(&bus->rxctl_lock);
  // spin_lock_init(&bus->txq_lock);
  bus->ctrl_wait = {};
  bus->dcmd_resp_wait = {};

  /* Initialize watchdog thread */
  bus->watchdog_wait = {};
  bus->watchdog_should_stop.store(false);
  thread_result =
      thrd_create_with_name(&bus->watchdog_tsk, &brcmf_sdio_watchdog_thread, bus, "brcmf-watchdog");

  if (thread_result != thrd_success) {
    BRCMF_ERR("brcmf_watchdog thread failed to start: error %d", thread_result);
    bus->watchdog_tsk = 0;
  }
  /* Initialize DPC thread */
  bus->dpc_triggered.store(false);
  bus->dpc_running = false;

  /* Assign bus interface call back */
  bus->sdiodev->bus_if->ops = &brcmf_sdio_bus_ops;
  bus->sdiodev->bus_if->chip = bus->ci->chip;
  bus->sdiodev->bus_if->chiprev = bus->ci->chiprev;

  /* default sdio bus header length for tx packet */
  bus->tx_hdrlen = SDPCM_HWHDR_LEN + SDPCM_SWHDR_LEN;

  /* Attach to the common layer, reserve hdr space */
  bus->sdiodev->drvr->bus_if = bus->sdiodev->bus_if;
  bus->sdiodev->drvr->settings = bus->sdiodev->settings;

  /* Set up the watchdog timer */
  bus->timer = new Timer(bus->sdiodev->drvr->device->GetDispatcher(),
                         std::bind(brcmf_sdio_watchdog, bus), false);

  ret = brcmf_attach(bus->sdiodev->drvr);
  if (ret != ZX_OK) {
    BRCMF_ERR("brcmf_attach failed");
    goto fail;
  }

  /* Attach and link in the protocol */
  ret = brcmf_proto_bcdc_attach(sdiodev->drvr);
  if (ret != ZX_OK) {
    BRCMF_ERR("brcmf_proto_bcdc_attach failed: %s", zx_status_get_string(ret));
    goto fail;
  }

  /* Query the F2 block size, set roundup accordingly */
  sdio_get_block_size(&sdiodev->sdio_proto_fn2, &bus->blocksize);
  bus->roundup = std::min(max_roundup, bus->blocksize);

  if (bus->sdiodev->bus_if->maxctl) {
    bus->sdiodev->bus_if->maxctl += bus->roundup;
  }

  sdio_claim_host(bus->sdiodev->func1);

  /* Disable F2 to clear any intermediate frame state on the dongle */
  sdio_disable_fn(&bus->sdiodev->sdio_proto_fn2);

  bus->rxflow = false;

  /* Done with backplane-dependent accesses, can drop clock... */
  brcmf_sdiod_func1_wb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);

  sdio_release_host(bus->sdiodev->func1);

  /* ...and initialize clock/power states */
  bus->clkstate = CLK_SDONLY;
  bus->idletime = BRCMF_IDLE_INTERVAL;
  bus->idleclock = BRCMF_IDLE_ACTIVE;

  /* SR state */
  bus->sr_enabled = false;

  BRCMF_DBG(SDIO, "completed!!");
  if (ret != ZX_OK) {
    goto fail;
  }

  return bus;

fail:
  brcmf_sdio_remove(bus);
  return NULL;
}

/* Detach and free everything */
void brcmf_sdio_remove(struct brcmf_sdio* bus) {
  BRCMF_DBG(TRACE, "Enter");

  if (bus) {
    /* De-register interrupt handler */
    brcmf_sdiod_intr_unregister(bus->sdiodev);
    brcmf_proto_bcdc_detach(bus->sdiodev->drvr);

    brcmf_detach(bus->sdiodev->drvr);

    bus->datawork.Cancel();
    if (bus->brcmf_wq) {
      delete bus->brcmf_wq;
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
      if (bus->sdiodev->settings->bus.sdio) {
        free(bus->sdiodev->settings->bus.sdio);
      }
      free(bus->sdiodev->settings);
    }

    delete bus->timer;
    delete bus->tx_queue;
    delete bus->rx_glom;
    free(bus->hdrbuf);
    delete bus;
  }

  BRCMF_DBG(TRACE, "Bus Disconnected");
}

/*Reset things When recovering from firmware crash*/
void brcmf_sdio_reset(struct brcmf_sdio* bus) {
  BRCMF_DBG(TRACE, "Enter");

  // Stop watch dog timer temporarily.
  brcmf_sdio_wd_timer(bus, false);

  // Clean up the data path.
  bus->datawork.Cancel();
  bus->brcmf_wq->Flush();

  // Flush tx queue.
  brcmu_pktq_flush(&bus->txq, true, NULL, NULL);

  // Restart watchdog timer
  brcmf_sdio_wd_timer(bus, true);
}

void brcmf_sdio_wd_timer(struct brcmf_sdio* bus, bool active) {
  /* Totally stop the timer */
  if (!active && bus->wd_active.load()) {
    bus->timer->Stop();
    bus->wd_active.store(false);
    return;
  }

  /* don't start the wd until fw is loaded */
  if (bus->sdiodev->state != BRCMF_SDIOD_DATA) {
    return;
  }

  if (active) {
    bus->wd_active.store(true);
    bus->timer->Start(ZX_MSEC(BRCMF_WD_POLL_MSEC));
  }
}

zx_status_t brcmf_sdio_sleep(struct brcmf_sdio* bus, bool sleep) {
  zx_status_t ret;

  sdio_claim_host(bus->sdiodev->func1);
  ret = brcmf_sdio_bus_sleep(bus, sleep, false);
  sdio_release_host(bus->sdiodev->func1);

  return ret;
}

static wlan::drivers::components::FrameContainer acquire_to_size(
    wlan::drivers::components::FrameStorage& storage, size_t size) {
  std::lock_guard lock(storage);
  if (storage.empty()) {
    return wlan::drivers::components::FrameContainer{};
  }
  const uint32_t frame_size = storage.front().Size();

  size_t num_frames = (size + frame_size - 1) / frame_size;
  wlan::drivers::components::FrameContainer frames = storage.Acquire(num_frames);
  // Adjust size of the last frame so that the total size matches the requested size
  size_t last_frame_size = size - ((num_frames - 1) * frame_size);
  frames.back().SetSize(last_frame_size);
  return frames;
}

static wlan::drivers::components::FrameStorage& get_rx_space_storage(struct brcmf_sdio* bus) {
  return bus->rx_tx_data.rx_space_valid ? bus->rx_tx_data.rx_space
                                        : bus->rx_tx_data.rx_space_internal;
}

wlan::drivers::components::FrameContainer brcmf_sdio_acquire_rx_space(struct brcmf_sdio* bus,
                                                                      size_t num) {
  auto& storage = get_rx_space_storage(bus);
  std::lock_guard lock(storage);
  return storage.Acquire(num);
}

std::optional<wlan::drivers::components::Frame> brcmf_sdio_acquire_single_rx_space(
    struct brcmf_sdio* bus) {
  auto& storage = get_rx_space_storage(bus);
  std::lock_guard lock(storage);
  return storage.Acquire();
}

wlan::drivers::components::FrameContainer brcmf_sdio_acquire_internal_rx_space(
    struct brcmf_sdio* bus, size_t num) {
  std::lock_guard lock(bus->rx_tx_data.rx_space_internal);
  return bus->rx_tx_data.rx_space_internal.Acquire(num);
}

std::optional<wlan::drivers::components::Frame> brcmf_sdio_acquire_single_internal_rx_space(
    struct brcmf_sdio* bus) {
  std::lock_guard lock(bus->rx_tx_data.rx_space_internal);
  return bus->rx_tx_data.rx_space_internal.Acquire();
}

wlan::drivers::components::FrameContainer brcmf_sdio_acquire_internal_rx_space_to_size(
    struct brcmf_sdio* bus, size_t size) {
  std::lock_guard lock(bus->rx_tx_data.rx_space);
  return acquire_to_size(bus->rx_tx_data.rx_space_internal, size);
}

wlan::drivers::components::FrameContainer brcmf_sdio_acquire_tx_space(struct brcmf_sdio* bus,
                                                                      size_t count) {
  std::lock_guard lock(bus->rx_tx_data.tx_space);
  return bus->rx_tx_data.tx_space.Acquire(count);
}

std::optional<wlan::drivers::components::Frame> brcmf_sdio_acquire_single_tx_space(
    struct brcmf_sdio* bus) {
  std::lock_guard lock(bus->rx_tx_data.tx_space);
  return bus->rx_tx_data.tx_space.Acquire();
}

wlan::drivers::components::FrameContainer brcmf_sdio_acquire_and_fill_tx_space(
    struct brcmf_sdio* bus, const void* data, size_t size) {
  wlan::drivers::components::FrameContainer frames =
      acquire_to_size(bus->rx_tx_data.tx_space, size);

  size_t remaining = size;
  size_t offset = 0;
  for (auto frame = frames.begin(); remaining > 0 && frame != frames.end(); ++frame) {
    const uint32_t size_to_copy = std::min<size_t>(remaining, frame->Size());
    memcpy(frame->Data(), reinterpret_cast<const uint8_t*>(data) + offset, size_to_copy);
    frame->SetSize(size_to_copy);
    offset += size_to_copy;
    remaining -= size_to_copy;
  }

  return frames;
}
