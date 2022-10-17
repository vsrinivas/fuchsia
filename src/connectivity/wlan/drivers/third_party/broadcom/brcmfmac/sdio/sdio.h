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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SDIO_SDIO_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SDIO_SDIO_H_

#include <fuchsia/hardware/gpio/c/banjo.h>
#include <fuchsia/hardware/sdio/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/sync/completion.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>

#include <optional>

#include <wlan/drivers/components/frame_storage.h>
#include <wlan/drivers/components/priority_queue.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_utils.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/defs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/linuxisms.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/timer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/workqueue.h"

#if !defined(NDEBUG)
// To see console logs remember to also enable the FWCON debug message filter in debug.h.
#define BRCMF_CONSOLE_LOG
#endif  // !defined(NDEBUG)

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

// TODO(cphoenix): Clean up all these names to make it clearer what's an address vs. a value.
#define SDIO_CCCR_INT_ENABLE 0x04
#define SDIO_CCCR_INTx 0x05
#define SDIO_CCCR_ABORT_RESET 0x06

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

/* Packet alignment for most efficient SDIO (can change based on platform) */
#define BRCMF_SDALIGN (1 << 6)

/* watchdog polling interval */
#define BRCMF_WD_POLL_MSEC (10)

/* A couple of SDIO device IDs that need special handling */
#define SDIO_DEVICE_ID_BROADCOM_4339 0x4339
#define SDIO_DEVICE_ID_BROADCOM_4335_4339 0x4335

#define SBSDIO_FORMAT_ADDR(addr)   \
  addr &= SBSDIO_SB_OFT_ADDR_MASK; \
  addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

#define DMA_ALIGNMENT 8

/* Maximum attempt times for firmware and nvram downloading */
#define FILE_LOAD_MAX_ATTEMPTS 5

/*
 * enum brcmf_sdiod_state - the state of the bus.
 *
 * @BRCMF_SDIOD_DOWN: Device can be accessed, no DPC.
 * @BRCMF_SDIOD_DATA: Ready for data transfers, DPC enabled.
 * @BRCMF_SDIOD_NOMEDIUM: No medium access to dongle possible.
 */
enum brcmf_sdiod_state { BRCMF_SDIOD_DOWN, BRCMF_SDIOD_DATA, BRCMF_SDIOD_NOMEDIUM };

enum {
  FRAGMENT_SDIO_FN1,
  FRAGMENT_SDIO_FN2,
  FRAGMENT_OOB_GPIO,
  FRAGMENT_DEBUG_GPIO,
  FRAGMENT_COUNT,
};

enum {
  WIFI_OOB_IRQ_GPIO_INDEX,
  DEBUG_GPIO_INDEX,
  GPIO_COUNT,
};

struct brcmf_sdreg {
  int func;
  int offset;
  int value;
};

/**
 * struct brcmf_sdio_pd - SDIO-specific device module parameters
 */
struct brcmf_sdio_pd {
  int txglomsz;
  int sd_sgentry_align;
  int sd_head_align;
  int drive_strength;
  int oob_irq_supported;
};

struct brcmf_sdio;

struct brcmf_sdio_dev {
  struct sdio_func* func1;
  struct sdio_func* func2;
  zx_duration_t ctl_done_timeout;
  uint32_t manufacturer_id;
  uint32_t product_id;
  sdio_protocol_t sdio_proto_fn1;
  sdio_protocol_t sdio_proto_fn2;
  gpio_protocol_t gpios[GPIO_COUNT];
  bool has_debug_gpio;
  zx_handle_t irq_handle;
  thrd_t isr_thread;
  struct brcmf_pub* drvr;
  uint32_t sbwad;             /* Save backplane window address */
  struct brcmf_core* cc_core; /* chipcommon core info struct */
  struct brcmf_sdio* bus;
  struct brcmf_bus* bus_if;
  struct brcmf_mp_device* settings;
  bool oob_irq_requested;
  bool sd_irq_requested;
  bool irq_wake; /* irq wake enable flags */
  bool txglom;
  uint32_t txglomsz;
  bool wowl_enabled;
  enum brcmf_sdiod_state state;
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

/* Get Bootloader MAC address. */
zx_status_t brcmf_sdiod_get_bootloader_macaddr(struct brcmf_sdio_dev* sdiodev, uint8_t* macaddr);

/* Register/deregister interrupt handler. */
zx_status_t brcmf_sdiod_intr_register(struct brcmf_sdio_dev* sdiodev);
void brcmf_sdiod_intr_unregister(struct brcmf_sdio_dev* sdiodev);

/* SDIO device register access interface for func0 and func1.
 * rb, rl - read byte / word and return value.
 * wb, wl - write byte / word.
 * Success is returned in result_out which may be NULL.
 */
uint8_t brcmf_sdiod_vendor_control_rb(struct brcmf_sdio_dev* sdiodev, uint8_t addr,
                                      zx_status_t* result_out);

void brcmf_sdiod_vendor_control_wb(struct brcmf_sdio_dev* sdiodev, uint8_t addr, uint8_t value,
                                   zx_status_t* result_out);

uint8_t brcmf_sdiod_func1_rb(struct brcmf_sdio_dev* sdiodev, uint32_t addr,
                             zx_status_t* result_out);

void brcmf_sdiod_func1_wb(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint8_t value,
                          zx_status_t* result_out);

uint32_t brcmf_sdiod_func1_rl(struct brcmf_sdio_dev* sdiodev, uint32_t addr,
                              zx_status_t* result_out);

void brcmf_sdiod_func1_wl(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint32_t data,
                          zx_status_t* result_out);

zx_status_t brcmf_sdiod_read(brcmf_sdio_dev* sdiodev, const sdio_protocol_t* proto, uint32_t addr,
                             void* data, size_t size, bool fifo);
zx_status_t brcmf_sdiod_write(brcmf_sdio_dev* sdiodev, const sdio_protocol_t* proto, uint32_t addr,
                              void* data, size_t size, bool fifo);

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
zx_status_t brcmf_sdiod_send_buf(struct brcmf_sdio_dev* sdiodev, uint8_t* buf, uint nbytes);
zx_status_t brcmf_sdiod_send_frames(struct brcmf_sdio_dev* sdiodev,
                                    cpp20::span<wlan::drivers::components::Frame>& frames,
                                    uint32_t* frames_sent_out);

zx_status_t brcmf_sdiod_recv_frames(struct brcmf_sdio_dev* sdiodev,
                                    cpp20::span<wlan::drivers::components::Frame> frames);

zx_status_t brcmf_sdiod_recv_frame(struct brcmf_sdio_dev* sdiodev,
                                   wlan::drivers::components::Frame& frame, uint32_t bytes);

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
                              void* data, size_t size);
// TODO(cphoenix): Expand "uint" to "unsigned int" everywhere.

/* Issue an abort to the specified function */
int brcmf_sdiod_abort(struct brcmf_sdio_dev* sdiodev, uint32_t func);

void brcmf_sdiod_change_state(struct brcmf_sdio_dev* sdiodev, enum brcmf_sdiod_state state);

struct brcmf_sdio* brcmf_sdio_probe(struct brcmf_sdio_dev* sdiodev);
zx_status_t brcmf_sdio_firmware_callback(brcmf_pub* drvr, const void* firmware,
                                         size_t firmware_size, const void* nvram,
                                         size_t nvram_size);
void brcmf_sdio_remove(struct brcmf_sdio* bus);
void brcmf_sdio_reset(struct brcmf_sdio* bus);
void brcmf_sdio_isr(struct brcmf_sdio* bus);

void brcmf_sdio_wd_timer(struct brcmf_sdio* bus, bool active);
zx_status_t brcmf_sdio_sleep(struct brcmf_sdio* bus, bool sleep);
void brcmf_sdio_trigger_dpc(struct brcmf_sdio* bus);
int brcmf_sdio_oob_irqhandler(void* cookie);

zx_status_t brcmf_sdio_register(brcmf_pub* drvr, std::unique_ptr<brcmf_bus>* out_bus);
void brcmf_sdio_exit(struct brcmf_bus* bus);

// The following definitions are made public for unit tests only. Note that sdio/BUILD.gn
// keeps the scope of these definitions relatively limited by declaring test/* as the
// only friends of this header file.
#define CTL_DONE_TIMEOUT_MSEC (2500)

#ifdef BRCMF_CONSOLE_LOG
struct rte_log_le {
  uint32_t buf; /* Can't be pointer on (64-bit) hosts */
  uint32_t buf_size;
  uint32_t idx;
  char* _buf_compat; /* Redundant pointer for backward compat. */
};

/* Device console log buffer state */
struct brcmf_console {
  uint count;               /* Poll interval msec counter */
  uint log_addr;            /* Log struct address (fixed) */
  struct rte_log_le log_le; /* Log struct (host copy) */
  uint bufsize;             /* Size of log buffer */
  uint8_t* buf;             /* Log buffer (host copy) */
  uint last;                /* Last buffer read index */
};

#endif /* BRCMF_CONSOLE_LOG */

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
  uint tx_qfull;          /* Count of tx attempts with queue full */
  uint fcqueued;          /* Tx packets that got queued */
  uint rxrtx;             /* Count of rtx requests (NAK to dongle) */
  uint rx_toolong;        /* Receive frames too long to receive */
  uint rxc_errors;        /* SDIO errors when reading control frames */
  uint rx_hdrfail;        /* SDIO errors on header reads */
  uint rx_badhdr;         /* Bad received headers (roosync?) */
  uint rx_badseq;         /* Mismatched rx sequence number */
  uint rx_outofbufs;      /* Ran out of RX buffers */
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

struct brcmf_tx_queue {
  explicit brcmf_tx_queue(size_t max_frames) : tx_queue(max_frames) {}
  wlan::drivers::components::PriorityQueue tx_queue __TA_GUARDED(txq_lock);
  size_t enqueue_count __TA_GUARDED(txq_lock) = 0;
  std::mutex txq_lock;
  wlan::drivers::components::FrameContainer tx_frames;
};

struct brcmf_rx_glom {
  brcmf_rx_glom() { glom_desc.SetSize(0); }
  wlan::drivers::components::Frame glom_desc;
  wlan::drivers::components::FrameContainer glom_frames;
  size_t glom_size;
  uint32_t glom_count;
};

struct brcmf_sdio_vmo_data {
  uint8_t id;
  zx_handle_t handle;
  uint64_t size;
};

struct brcmf_sdio_rx_tx_data {
  // VMOs
  brcmf_sdio_vmo_data vmos[MAX_VMOS] __TA_GUARDED(vmos_mutex);
  std::mutex vmos_mutex;
  std::array<uint8_t*, MAX_VMOS> vmo_addrs;

  // Frame storage for receiving before we have queued RX space from netdev
  wlan::drivers::components::FrameStorage rx_space;
  wlan::drivers::components::FrameStorage rx_space_internal;
  bool rx_space_valid = false;

  wlan::drivers::components::FrameStorage tx_space;

  // VMO used for internal TX/RX buffers, this is for data that is not exposed outside the driver.
  zx::vmo internal_vmo;

  // Frames from internal storage that are used for small, frequent tx/rx on func1. Concurrent
  // access to these will be protected by claim_host/release_host locking for func1.
  wlan::drivers::components::Frame rx_frame;
  wlan::drivers::components::Frame tx_frame;
};

/* misc chip info needed by some of the routines */
/* Private data for SDIO bus interaction */
struct brcmf_sdio {
  struct brcmf_sdio_dev* sdiodev; /* sdio device handler */
  struct brcmf_chip* ci;          /* Chip info struct */
  struct brcmf_core* sdio_core;   /* sdio core info struct */

  uint32_t hostintmask;       /* Copy of Host Interrupt Mask */
  std::atomic<int> intstatus; /* Intstatus bits (events) pending */
  std::atomic<int> fcstate;   /* State of dongle flow-control */

  uint16_t blocksize; /* Block size of SDIO transfers */
  uint roundup;       /* Max roundup limit */

  struct pktq txq;     /* Queue length used for flow-control */
  struct brcmf_tx_queue* tx_queue;

  uint8_t flowcontrol; /* per prio flow control bitmask */
  uint8_t tx_seq;      /* Transmit sequence number (next) */
  uint8_t tx_max;      /* Maximum transmit sequence allowed */

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

  struct brcmf_rx_glom* rx_glom;

  /* ctrl frame resources, protected by bus->sdiodev->drvr->irq_callback_lock */
  std::vector<uint8_t> rx_ctl_frame;

  uint8_t sdpcm_ver; /* Bus protocol reported by dongle */

  bool intr;              /* Use interrupts */
  bool poll;              /* Use polling */
  std::atomic<int> ipend; /* Device interrupt is pending */
  uint spurious;          /* Count of spurious interrupts */
  uint pollrate;          /* Ticks between device polls */
  uint polltick;          /* Tick counter */

#ifdef BRCMF_CONSOLE_LOG
  uint console_interval;
  struct brcmf_console console; /* Console output polling support */
  uint console_addr;            /* Console address from shared struct */
#endif // BRCMF_CONSOLE_LOG

  uint clkstate;     /* State of sd and backplane clock(s) */
  int32_t idletime;  /* Control for activity timeout */
  int32_t idlecount; /* Activity timeout counter */
  int32_t idleclock; /* How to set bus driver when idle */
  bool rxflow_mode;  /* Rx flow control mode */
  bool rxflow;       /* Is rx flow control on */
  bool alp_only;     /* Don't use HT clock (ALP only) */

  uint8_t* ctrl_frame_buf;
  uint16_t ctrl_frame_len;
  std::atomic<bool> ctrl_frame_stat;
  zx_status_t ctrl_frame_err;

  // spinlock_t txq_lock; /* protect bus->txq */
  sync_completion_t ctrl_wait;
  sync_completion_t dcmd_resp_wait;

  Timer* timer;
  sync_completion_t watchdog_wait;
  std::atomic<bool> watchdog_should_stop;
  thrd_t watchdog_tsk;
  std::atomic<bool> wd_active;

  WorkQueue* brcmf_wq;
  WorkItem datawork;
  std::atomic<bool> dpc_triggered;
  bool dpc_running;

  bool txoff; /* Transmit flow-controlled */
  struct brcmf_sdio_count sdcnt;
  bool sr_enabled; /* SaveRestore enabled */
  bool sleeping;

  uint8_t tx_hdrlen;      /* sdio bus header length for tx packet */
  bool txglom;
  uint16_t head_align;    /* buffer pointer alignment */
  uint16_t sgentry_align; /* scatter-gather buffer alignment */

  brcmf_sdio_rx_tx_data rx_tx_data;
};

void brcmf_sdio_if_ctrl_frame_stat_set(struct brcmf_sdio* bus, std::function<void()> fn);
void brcmf_sdio_if_ctrl_frame_stat_clear(struct brcmf_sdio* bus, std::function<void()> fn);
void brcmf_sdio_wait_event_wakeup(struct brcmf_sdio* bus);
zx_status_t brcmf_sdio_bus_txctl(brcmf_bus* bus_if, unsigned char* msg, uint msglen);

// Load firmware, nvram and CLM binary files. The parameter "reload" means whether this function is
// called for the firmware reloading process when it crashes.
zx_status_t brcmf_sdio_load_files(brcmf_pub* drvr, bool reload);

wlan::drivers::components::FrameContainer brcmf_sdio_acquire_rx_space(struct brcmf_sdio* bus,
                                                                      size_t num);
std::optional<wlan::drivers::components::Frame> brcmf_sdio_acquire_single_rx_space(
    struct brcmf_sdio* bus);

wlan::drivers::components::FrameContainer brcmf_sdio_acquire_internal_rx_space(
    struct brcmf_sdio* bus, size_t num);
std::optional<wlan::drivers::components::Frame> brcmf_sdio_acquire_single_internal_rx_space(
    struct brcmf_sdio* bus);
wlan::drivers::components::FrameContainer brcmf_sdio_acquire_internal_rx_space_to_size(
    struct brcmf_sdio* bus, size_t size);

wlan::drivers::components::FrameContainer brcmf_sdio_acquire_tx_space(struct brcmf_sdio* bus,
                                                                      size_t count);
std::optional<wlan::drivers::components::Frame> brcmf_sdio_acquire_single_tx_space(
    struct brcmf_sdio* bus);

// Acquire TX space with enough space for |size| bytes and copy |data| into the TX space. This
// may acquire multiple frames if an individual frame cannot hold |size| bytes.
wlan::drivers::components::FrameContainer brcmf_sdio_acquire_and_fill_tx_space(
    struct brcmf_sdio* bus, const void* data, size_t size);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SDIO_SDIO_H_
