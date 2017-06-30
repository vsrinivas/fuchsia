// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <hw/reg.h>

#define XHCI_READ32(a)      readl(a)
#define XHCI_WRITE32(a, v)  writel(v, a)
#define XHCI_READ64(a)      readll(a)
#define XHCI_WRITE64(a, v)  writell(v, a)

#define XHCI_SET32(addr, mask, bits) XHCI_WRITE32(addr, (XHCI_READ32(addr) & ~(mask)) | ((bits) & (mask)))

#define XHCI_MASK(start, count) (((1 << (count)) - 1) << (start))
#define XHCI_GET_BITS32(src, start, count) ((XHCI_READ32(src) & XHCI_MASK(start, count)) >> (start))
#define XHCI_SET_BITS32(dest, start, count, value) \
            XHCI_WRITE32(dest, (XHCI_READ32(dest) & ~XHCI_MASK(start, count)) | \
                                (((value) << (start)) & XHCI_MASK(start, count)))

// Max number of endpoints per device
#define XHCI_NUM_EPS    32

// Data buffers for TRBs are limited to 64K
#define XHCI_MAX_DATA_BUFFER    65536

// XHCI Capability Registers
typedef struct {
    uint8_t length;         // Capability Register Length
    uint8_t reserved;
    uint16_t hciversion;    // Interface Version Number
    uint32_t hcsparams1;    // Structural Parameters 1
    uint32_t hcsparams2;    // Structural Parameters 2
    uint32_t hcsparams3;    // Structural Parameters 3
    uint32_t hccparams1;    // Capability Parameters 1
    uint32_t dboff;         // Doorbell Offset
    uint32_t rtsoff;        // Runtime Register Space Offset
    uint32_t hccparams2;    // Capability Parameters 2
} __PACKED xhci_cap_regs_t;

// XHCI Port Register Set
typedef volatile struct {
    uint32_t portsc;        // Port Status and Control
    uint32_t portpmsc;      // Port Power Management Status and Control
    uint32_t portli;        // Port Link Info
    uint32_t portlpmc;      // Port Hardware LPM Control
} __PACKED xhci_port_regs_t;

// XHCI Operational Registers
typedef volatile struct {
    uint32_t usbcmd;        // USB Command
    uint32_t usbsts;        // USB Status
    uint32_t pagesize;      // Page Size
    uint8_t reserved1[8];
    uint32_t dnctrl;        // Device Notification Control
    uint64_t crcr;          // Command Ring Control
    uint8_t reserved2[16];
    uint64_t dcbaap;        // Device Context Base Address Array Pointer
    uint32_t config;        // Configure
    uint8_t reserved3[964];
    xhci_port_regs_t port_regs[];
} __PACKED xhci_op_regs_t;

// XHCI Interrupter Registers
typedef volatile struct {
    uint32_t    iman;       // Interrupter Management
    uint32_t    imod;       // Interrupter Moderation
    uint32_t    erstsz;     // Event Ring Segment Table Size
    uint32_t    reserved;
    uint64_t    erstba;     // Event Ring Segment Table Base Address
    uint64_t    erdp;       // Event Ring Dequeue Pointer
} __PACKED xhci_intr_regs_t;

// XHCI Runtime Registers
typedef volatile struct {
    uint32_t    mfindex;    // Microframe Index Register
    uint32_t    reserved[7];
    xhci_intr_regs_t intr_regs[1024];
} __PACKED xhci_runtime_regs_t;
#define XHCI_MFINDEX_BITS   14

// Slot Context
typedef volatile struct {
    uint32_t sc0;
    uint32_t sc1;
    uint32_t sc2;
    uint32_t sc3;
    uint32_t reserved[4];
} __PACKED xhci_slot_context_t;

// Endpoint Context
typedef volatile struct {
    uint32_t epc0;
    uint32_t epc1;
    uint32_t epc2;
    uint32_t tr_dequeue_hi;
    uint32_t epc4;
    uint32_t reserved[3];
} __PACKED xhci_endpoint_context_t;

// Stream Context
typedef volatile struct {
    uint32_t sc0;
    uint32_t sc1;
    uint32_t sc2;
    uint32_t reserved;
} __PACKED xhci_stream_context_t;

// Input Control Context
typedef volatile struct {
    uint32_t drop_context_flags;
    uint32_t add_context_flags;
    uint32_t reserved[5];
    uint32_t icc7;
} __PACKED xhci_input_control_context_t;

// Transfer Request Block
typedef volatile struct {
    union {
    uint64_t ptr;
        struct {
            uint32_t ptr_low;
            uint32_t ptr_high;
        } __PACKED;
    } __PACKED;
    uint32_t status;
    uint32_t control;
} __PACKED xhci_trb_t;

// Event Ring Segment Table Entry
typedef volatile struct {
    uint64_t ptr;
    uint32_t size;
    uint32_t reserved;
} __PACKED erst_entry_t;

// XHCI USB Legacy Support Extended Cap
typedef volatile struct {
    uint8_t cap_id;
    uint8_t next_cap_ptr;
    uint8_t bios_owned_sem;
    uint8_t os_owned_sem;
} __PACKED xhci_usb_legacy_support_cap_t;

// Command register bits
#define USBCMD_RS       (1 << 0)
#define USBCMD_HCRST    (1 << 1)
#define USBCMD_INTE     (1 << 2)
#define USBCMD_HSEE     (1 << 3)
#define USBCMD_LHCRST   (1 << 7)
#define USBCMD_CSS      (1 << 8)
#define USBCMD_CRS      (1 << 9)
#define USBCMD_EWE      (1 << 10)
#define USBCMD_EU3S     (1 << 11)
#define USBCMD_CME      (1 << 12)

// Status register bits
#define USBSTS_HCH      (1 << 0)
#define USBSTS_HSE      (1 << 2)
#define USBSTS_EINT     (1 << 3)
#define USBSTS_PCD      (1 << 4)
#define USBSTS_SSS      (1 << 8)
#define USBSTS_RSS      (1 << 9)
#define USBSTS_SRE      (1 << 10)
#define USBSTS_CNR      (1 << 11)
#define USBSTS_HCE      (1 << 12)

#define USBSTS_CLEAR_BITS (USBSTS_HCH | USBSTS_HSE | USBSTS_EINT | USBSTS_PCD | USBSTS_SSS | \
                           USBSTS_RSS | USBSTS_SRE | USBSTS_CNR | USBSTS_HCE)

// CONFIG register bits
#define CONFIG_MAX_SLOTS_ENABLED_START  0
#define CONFIG_MAX_SLOTS_ENABLED_BITS   8
#define CONFIG_U3E      (1 << 8)
#define CONFIG_CIE      (1 << 9)

// HCSPARAMS1 register bits
#define HCSPARAMS1_MAX_SLOTS_START  0
#define HCSPARAMS1_MAX_SLOTS_BITS   8
#define HCSPARAMS1_MAX_INTRS_START  8
#define HCSPARAMS1_MAX_INTRS_BITS   11
#define HCSPARAMS1_MAX_PORTS_START  24
#define HCSPARAMS1_MAX_PORTS_BITS   8

// HCSPARAMS2 register bits
#define HCSPARAMS2_IST_BITS             4
#define HCSPARAMS2_ERST_MAX_START       4
#define HCSPARAMS2_ERST_MAX_BITS        4
#define HCSPARAMS2_MAX_SBBUF_HI_START   21
#define HCSPARAMS2_MAX_SBBUF_HI_BITS    5
#define HCSPARAMS2_SPR_START            26
#define HCSPARAMS2_SPR_BITS             1
#define HCSPARAMS2_MAX_SBBUF_LO_START   27
#define HCSPARAMS2_MAX_SBBUF_LO_BITS    5

// HCCPARAMS1 register bits
#define HCCPARAMS1_AC64                 (1 << 0)
#define HCCPARAMS1_BNC                  (1 << 1)
#define HCCPARAMS1_CSZ                  (1 << 2)
#define HCCPARAMS1_PPC                  (1 << 3)
#define HCCPARAMS1_PIND                 (1 << 4)
#define HCCPARAMS1_LHRC                 (1 << 5)
#define HCCPARAMS1_LTC                  (1 << 6)
#define HCCPARAMS1_NSS                  (1 << 7)
#define HCCPARAMS1_PAE                  (1 << 8)
#define HCCPARAMS1_SPC                  (1 << 9)
#define HCCPARAMS1_SEC                  (1 << 10)
#define HCCPARAMS1_CFC                  (1 << 11)
#define HCCPARAMS1_MAX_PSA_SIZE_START   12
#define HCCPARAMS1_MAX_PSA_SIZE_BITS    4
#define HCCPARAMS1_EXT_CAP_PTR_START    16
#define HCCPARAMS1_EXT_CAP_PTR_BITS     16

// HCCPARAMS2 register bits
#define HCCPARAMS2_U3C  (1 << 0)    // U3 Entry Capability
#define HCCPARAMS2_CMC  (1 << 1)    // Configure Endpoint Command Max Exit Latency Too Large Capability
#define HCCPARAMS2_FSC  (1 << 2)    // Force Save Context Capability
#define HCCPARAMS2_CTC  (1 << 3)    // Compliance Transition Capability
#define HCCPARAMS2_LEC  (1 << 4)    // Large ESIT Payload Capability
#define HCCPARAMS2_CIC  (1 << 5)    // Configuration Information Capability

// XHCI Extended Capabilities register
#define EXT_CAP_CAPABILITY_ID_START     0
#define EXT_CAP_CAPABILITY_ID_BITS      8
#define EXT_CAP_NEXT_PTR_START          8
#define EXT_CAP_NEXT_PTR_BITS           8

// XHCI Extended Capability codes
#define EXT_CAP_USB_LEGACY_SUPPORT      1
#define EXT_CAP_SUPPORTED_PROTOCOL      2
#define EXT_CAP_EXT_POWER_MANAGEMENT    3
#define EXT_CAP_IO_VIRTUALIZATION       4
#define EXT_CAP_MESSAGE_INTERRUPT       5
#define EXT_CAP_LOCAL_MEMORY            6
#define EXT_CAP_USB_DEBUG_CAPABILITY    10
#define EXT_CAP_EXT_MESSAGE_INTERRUPT   17

// XHCI Supported Protocol Capability bits (word 0)
#define EXT_CAP_SP_REV_MINOR_START      16
#define EXT_CAP_SP_REV_MINOR_BITS       8
#define EXT_CAP_SP_REV_MAJOR_START      24
#define EXT_CAP_SP_REV_MAJOR_BITS       8

// XHCI Supported Protocol Capability bits (word 2)
#define EXT_CAP_SP_COMPAT_PORT_OFFSET_START 0
#define EXT_CAP_SP_COMPAT_PORT_OFFSET_BITS  8
#define EXT_CAP_SP_COMPAT_PORT_COUNT_START  8
#define EXT_CAP_SP_COMPAT_PORT_COUNT_BITS   8
#define EXT_CAP_SP_PSIC_START               28
#define EXT_CAP_SP_PSIC_BITS                4

// XHCI Supported Protocol Speed ID (PSI) bits
#define EXT_CAP_SP_PSIV_START           0
#define EXT_CAP_SP_PSIV_BITS            4
#define EXT_CAP_SP_PSIE_START           4
#define EXT_CAP_SP_PSIE_BITS            2
#define EXT_CAP_SP_PLT_START            6
#define EXT_CAP_SP_PLT_BITS             2
#define EXT_CAP_SP_PFD                  (1 << 8)
#define EXT_CAP_SP_PLT_START            6
#define EXT_CAP_SP_PSIM_START           16
#define EXT_CAP_SP_PSIM_BITS            16

// Command Ring Control Register bits
#define CRCR_RCS        (1 << 0)
#define CRCR_CS         (1 << 1)
#define CRCR_CA         (1 << 2)
#define CRCR_CRR        (1 << 3)

// Interrupter register bits
#define IMAN_IP         (1 << 0)    // Interrupt Pending
#define IMAN_IE         (1 << 1)    // Interrupt Enable
#define IMODI_MASK      0x0000FFFF  // Interrupter Moderation Interval
#define IMODC_MASK      0xFFFF0000  // Interrupter Moderation Counter
#define ERSTSZ_MASK     0x0000FFFF
#define ERDP_DESI_START 0           // First bit of Dequeue ERST Segment Index
#define ERDP_DESI_BITS  2           // Bit length of Dequeue ERST Segment Index
#define ERDP_EHB        (1 << 3)    // Event Handler Busy (set this bit to clear)

// PORTSC bits
#define PORTSC_CCS          (1 << 0)    // Current Connect Status
#define PORTSC_PED          (1 << 1)    // Port Enabled/Disabled
#define PORTSC_OCA          (1 << 3)    // Over-current Active
#define PORTSC_PR           (1 << 4)    // Port Reset
#define PORTSC_PLS_START    5           // Port Link State
#define PORTSC_PLS_BITS     4
#define PORTSC_PP           (1 << 9)    // Port Power
#define PORTSC_SPEED_START  10          // Port Speed
#define PORTSC_SPEED_BITS   4
#define PORTSC_PIC_START    14          // Port Indicator Control
#define PORTSC_PIC_BITS     2
#define PORTSC_LWS          (1 << 16)   // Port Link State Write Strobe
#define PORTSC_CSC          (1 << 17)   // Connect Status Change
#define PORTSC_PEC          (1 << 18)   // Port Enabled/Disabled Change
#define PORTSC_WRC          (1 << 19)   // Warm Port Reset Change
#define PORTSC_OCC          (1 << 20)   // Over-current Change
#define PORTSC_PRC          (1 << 21)   // Port Reset Change
#define PORTSC_PLC          (1 << 22)   // Port Link State Change
#define PORTSC_CEC          (1 << 23)   // Port Config Error Change
#define PORTSC_CAS          (1 << 24)   // Cold Attach Status
#define PORTSC_WCE          (1 << 25)   // Wake on Connect Enable
#define PORTSC_WDE          (1 << 26)   // Wake on Disconnect Enable
#define PORTSC_WOE          (1 << 27)   // Wake on Over-current Enable
#define PORTSC_DR           (1 << 30)   // Device Removable
#define PORTSC_WPR          (1 << 31)   // Warm Port Reset

// PORTSC control bits
#define PORTSC_CONTROL_BITS (PORTSC_PR | PORTSC_PP | PORTSC_LWS | \
                             PORTSC_WCE | PORTSC_WDE | PORTSC_WOE | \
                             XHCI_MASK(PORTSC_PLS_START, PORTSC_PLS_BITS) | \
                             XHCI_MASK(PORTSC_PIC_START, PORTSC_PIC_BITS))

// PORTSC status bits, set to clear
#define PORTSC_STATUS_BITS  (PORTSC_CSC | PORTSC_PEC | PORTSC_WRC | PORTSC_OCC | \
                             PORTSC_PRC | PORTSC_PLC | PORTSC_CEC | PORTSC_CAS)

// TRB types
#define TRB_TRANSFER_NORMAL             1
#define TRB_TRANSFER_SETUP              2
#define TRB_TRANSFER_DATA               3
#define TRB_TRANSFER_STATUS             4
#define TRB_TRANSFER_ISOCH              5
#define TRB_LINK                        6
#define TRB_TRANSFER_EVENT_DATA         7
#define TRB_TRANSFER_NOOP               8
#define TRB_CMD_ENABLE_SLOT             9
#define TRB_CMD_DISABLE_SLOT            10
#define TRB_CMD_ADDRESS_DEVICE          11
#define TRB_CMD_CONFIGURE_EP            12
#define TRB_CMD_EVAL_CONTEXT            13
#define TRB_CMD_RESET_ENDPOINT          14
#define TRB_CMD_STOP_ENDPOINT           15
#define TRB_CMD_SET_TR_DEQUEUE          16
#define TRB_CMD_RESET_DEVICE            17
#define TRB_CMD_FORCE_EVENT             18
#define TRB_CMD_NEGOTIATE_BW            19
#define TRB_CMD_SET_LATENCY             20
#define TRB_CMD_GET_PORT_BW             21
#define TRB_CMD_FORCE_HEADER            22
#define TRB_CMD_NOOP                    23
#define TRB_EVENT_TRANSFER              32
#define TRB_EVENT_COMMAND_COMP          33
#define TRB_EVENT_PORT_STATUS_CHANGE    34
#define TRB_EVENT_BANDWITH_REQ          35
#define TRB_EVENT_DOORBELL              36
#define TRB_EVENT_HOST_CONTROLLER       37
#define TRB_EVENT_DEVICE_NOTIFY         38
#define TRB_EVENT_MFINDEX_WRAP          39

// TRB condition codes
#define TRB_CC_SUCCESS                      1
#define TRB_CC_DATA_BUFFER_ERROR            2
#define TRB_CC_BABBLE_DETECTED_ERROR        3
#define TRB_CC_USB_TRANSACTION_ERROR        4
#define TRB_CC_TRB_ERROR                    5
#define TRB_CC_STALL_ERROR                  6
#define TRB_CC_RESOURCE_ERROR               7
#define TRB_CC_BANDWIDTH_ERROR              8
#define TRB_CC_NO_SLOTS_AVAILABLE_ERROR     9
#define TRB_CC_INVALID_STREAM_TYPE_ERROR    10
#define TRB_CC_SLOT_NOT_ENABLED_ERROR       11
#define TRB_CC_ENDPOINT_NOT_ENABLED_ERROR   12
#define TRB_CC_SHORT_PACKET                 13
#define TRB_CC_RING_UNDERRUN                14
#define TRB_CC_RING_OVERRUN                 15
#define TRB_CC_VF_EVENT_RING_FULL_ERROR     16
#define TRB_CC_PARAMETER_ERROR              17
#define TRB_CC_BANDWIDTH_OVERRUN_ERROR      18
#define TRB_CC_CONTEXT_STATE_ERROR          19
#define TRB_CC_NO_PING_RESPONSE_ERROR       20
#define TRB_CC_EVENT_RING_FULL_ERROR        21
#define TRB_CC_INCOMPATIBLE_DEVICE_ERROR    22
#define TRB_CC_MISSED_SERVICE_ERROR         23
#define TRB_CC_COMMAND_RING_STOPPED         24
#define TRB_CC_COMMAND_ABORTED              25
#define TRB_CC_STOPPED                      26
#define TRB_CC_STOPPED_LENGTH_INVALID       27
#define TRB_CC_STOPPED_SHORT_PACKET         28
#define TRB_CC_MAX_EXIT_LATENCY_ERROR       29
#define TRB_CC_ISOCH_BUFFER_OVERRUN         31
#define TRB_CC_EVENT_LOST_ERROR             32
#define TRB_CC_UNDEFINED_ERROR              33
#define TRB_CC_INVALID_STREAM_ID_ERROR      34
#define TRB_CC_SECONDARY_BANDWIDTH_ERROR    35
#define TRB_CC_SPLIT_TRANSACTION_ERROR      36

// TRB type is in bits 10 - 15 of TRB control field
#define TRB_TYPE_START              10
#define TRB_TYPE_BITS               6
#define TRB_TYPE_MASK               (((1 << TRB_TYPE_BITS) - 1) << TRB_TYPE_START)

// TRB Flags (bits on TRB control field)
#define TRB_C       (1 << 0)    // Marks enqueue pointer location
#define TRB_TC      (1 << 1)    // Toggles interpretation of cycle bit
#define TRB_CHAIN   (1 << 4)    // Associates TRB with next TRB
#define TRB_BSR     (1 << 9)    // Block Set Address Request

// Event TRB bits
#define EVT_TRB_CCP_START           0   // Command Completion Parameter
#define EVT_TRB_CCP_BITS            24
#define EVT_TRB_CC_START            24   // Completion Code (also used for Transfer event TRBs)
#define EVT_TRB_CC_BITS             8

// Port Status Change Event TRB bits
#define EVT_TRB_PORT_ID_START       24   // ID of root hub port that changed
#define EVT_TRB_PORT_ID_BITS        8

// Transfer event TRB bits
#define EVT_TRB_XFER_LENGTH_START   0
#define EVT_TRB_XFER_LENGTH_BITS    24
#define EVT_TRB_EP_ID_START         16
#define EVT_TRB_EP_ID_BITS          5
#define EVT_TRB_ED                  (1 << 2) // event was generated by event data TRB

// Transfer TRB bits
#define SETUP_TRB_REQ_TYPE_START    0
#define SETUP_TRB_REQ_TYPE_BITS     8
#define SETUP_TRB_REQUEST_START     8
#define SETUP_TRB_REQUEST_BITS      8
#define SETUP_TRB_VALUE_START       16
#define SETUP_TRB_VALUE_BITS        16
#define SETUP_TRB_INDEX_START       0
#define SETUP_TRB_INDEX_BITS        16
#define SETUP_TRB_LENGTH_START      16
#define SETUP_TRB_LENGTH_BITS       16
#define XFER_TRB_XFER_LENGTH_START  0
#define XFER_TRB_XFER_LENGTH_BITS   17
#define XFER_TRB_TD_SIZE_START      17
#define XFER_TRB_TD_SIZE_BITS       5
#define XFER_TRB_INTR_TARGET_START  22
#define XFER_TRB_INTR_TARGET_BITS   10
#define XFER_TRB_ENT                (1 << 1)
#define XFER_TRB_ISP                (1 << 2)
#define XFER_TRB_NS                 (1 << 3)
#define XFER_TRB_CH                 (1 << 4)
#define XFER_TRB_IOC                (1 << 5)    // Interrupt On Completion
#define XFER_TRB_IDT                (1 << 6)    // Immediate Data
#define XFER_TRB_DIR                (1 << 16)   // Transfer direction (0 = out, 1 = in)
#define XFER_TRB_DIR_IN             XFER_TRB_DIR
#define XFER_TRB_DIR_OUT            0
#define XFER_TRB_TRT_START          16          // Transfer type
#define XFER_TRB_TRT_BITS           2

// Isoch Transfer TRB bits
#define XFER_TRB_SIA               (1 << 31)    // Schedule packet ASAP
#define XFER_TRB_FRAME_ID_START    20
#define XFER_TRB_FRAME_ID_BITS     11
#define XFER_TRB_TLBPC_START       16
#define XFER_TRB_TLBPC_BITS        4
#define XFER_TRB_BEI               (1 << 9)
#define XFER_TRB_FRAME_TBC_START   7
#define XFER_TRB_FRAME_TBC_BITS    2

// Preshifted TRT bits
#define XFER_TRB_TRT_NONE          (0 << XFER_TRB_TRT_START)
#define XFER_TRB_TRT_OUT           (2 << XFER_TRB_TRT_START)
#define XFER_TRB_TRT_IN            (3 << XFER_TRB_TRT_START)

// For various TRBs
#define TRB_SLOT_ID_START           24
#define TRB_SLOT_ID_BITS            8
#define TRB_ENDPOINT_ID_START       16
#define TRB_ENDPOINT_ID_BITS        5

// Slot context bits (sc0)
#define SLOT_CTX_ROUTE_STRING_START         0
#define SLOT_CTX_ROUTE_STRING_BITS          20
#define SLOT_CTX_SPEED_START                20
#define SLOT_CTX_SPEED_BITS                 4
#define SLOT_CTX_MTT_START                  25
#define SLOT_CTX_MTT_BITS                   1
#define SLOT_CTX_HUB                        (1 << 26)
#define SLOT_CTX_CONTEXT_ENTRIES_START      27
#define SLOT_CTX_CONTEXT_ENTRIES_BITS       5

// Slot context bits (sc1)
#define SLOT_CTX_MAX_EXIT_LATENCY_START     0
#define SLOT_CTX_MAX_EXIT_LATENCY_BITS      16
#define SLOT_CTX_ROOT_HUB_PORT_NUM_START    16
#define SLOT_CTX_ROOT_HUB_PORT_NUM_BITS     8
#define SLOT_CTX_ROOT_NUM_PORTS_START       24
#define SLOT_CTX_ROOT_NUM_PORTS_BITS        8

// Slot context bits (sc2)
#define SLOT_CTX_TT_HUB_SLOT_ID_START       0
#define SLOT_CTX_TT_HUB_SLOT_ID_BITS        8
#define SLOT_CTX_TT_PORT_NUM_START          8
#define SLOT_CTX_TT_PORT_NUM_BITS           8
#define SLOT_CTX_TTT_START                  16
#define SLOT_CTX_TTT_BITS                   2
#define SLOT_CTX_INTERRUPTER_TARGET_START   22
#define SLOT_CTX_INTERRUPTER_TARGET_BITS    10

// Slot context bits (sc3)
#define SLOT_CTX_DEVICE_ADDRESS_START       0
#define SLOT_CTX_DEVICE_ADDRESS_BITS        8
#define SLOT_CTX_SLOT_STATE_START           27
#define SLOT_CTX_SLOT_STATE_BITS            5

// Endpoint context bits (ec0)
#define EP_CTX_EP_STATE_START               0
#define EP_CTX_EP_STATE_BITS                3
#define EP_CTX_MULT_START                   8
#define EP_CTX_MULT_BITS                    2
#define EP_CTX_MAX_P_STREAMS_START          10
#define EP_CTX_MAX_P_STREAMS_BITS           5
#define EP_CTX_LSA                          (1 << 15)
#define EP_CTX_INTERVAL_START               16
#define EP_CTX_INTERVAL_BITS                8
#define EP_CTX_MAX_ESIT_PAYLOAD_HI_START    24
#define EP_CTX_MAX_ESIT_PAYLOAD_HI_BITS     8

// EP_CTX_EP_STATE values
#define EP_CTX_STATE_DISABLED               0
#define EP_CTX_STATE_RUNNING                1
#define EP_CTX_STATE_HALTED                 2
#define EP_CTX_STATE_STOPPED                3
#define EP_CTX_STATE_ERROR                  4

// Endpoint context bits (epc1)
#define EP_CTX_CERR_START                   1
#define EP_CTX_CERR_BITS                    2
#define EP_CTX_EP_TYPE_START                3
#define EP_CTX_EP_TYPE_BITS                 3
#define EP_CTX_HID                          (1 << 7)
#define EP_CTX_MAX_BURST_SIZE_START         8
#define EP_CTX_MAX_BURST_SIZE_BITS          8
#define EP_CTX_MAX_PACKET_SIZE_START        16
#define EP_CTX_MAX_PACKET_SIZE_BITS         16

// EP_CTX_EP_TYPE values
#define EP_CTX_EP_TYPE_ISOCH_OUT            1
#define EP_CTX_EP_TYPE_BULK_OUT             2
#define EP_CTX_EP_TYPE_INTERRUPT_OUT        3
#define EP_CTX_EP_TYPE_CONTROL              4
#define EP_CTX_EP_TYPE_ISOCH_IN             5
#define EP_CTX_EP_TYPE_BULK_IN              6
#define EP_CTX_EP_TYPE_INTERRUPT_IN         7

// Endpoint context bits (epc2)
#define EP_CTX_DCS                          (1 << 0)
#define EP_CTX_TR_DEQUEUE_LO_MASK           0xFFFFFFF0

// Endpoint context bits (epc4)
#define EP_CTX_AVG_TRB_LENGTH_START         0
#define EP_CTX_AVG_TRB_LENGTH_BITS          16
#define EP_CTX_MAX_ESIT_PAYLOAD_LO_START    16
#define EP_CTX_MAX_ESIT_PAYLOAD_LO_BITS     16

// for input control context add and drop context flags
#define XHCI_ICC_SLOT_FLAG          (1 << 0)
#define XHCI_ICC_EP_FLAG(ep)        (1 << ((ep) + 1))

static inline uint32_t trb_get_type(xhci_trb_t* trb) {
    return XHCI_GET_BITS32(&trb->control, TRB_TYPE_START, TRB_TYPE_BITS);
}

static inline void* trb_get_ptr(xhci_trb_t* trb) {
#if (UINTPTR_MAX == UINT32_MAX)
    return (void *)(uint32_t)XHCI_READ64(&trb->ptr);
#else
    return (void *)XHCI_READ64(&trb->ptr);
#endif
}

static inline void trb_set_ptr(xhci_trb_t* trb, void* ptr) {
#if (UINTPTR_MAX == UINT32_MAX)
    XHCI_WRITE64(&trb->ptr, (uint32_t)ptr);
#else
    XHCI_WRITE64(&trb->ptr, (uint64_t)ptr);
#endif
}

static inline void trb_set_control(xhci_trb_t* trb, uint32_t type, uint32_t flags) {
    XHCI_WRITE32(&trb->control, ((type << TRB_TYPE_START) & TRB_TYPE_MASK) | flags);
}
