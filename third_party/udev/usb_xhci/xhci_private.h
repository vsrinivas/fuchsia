/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2010 Patrick Georgi
 * Copyright (C) 2013 secunet Security Networks AG
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io_alloc.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/usb_bus.h>
#include <ddk/protocol/usb_hci.h>
#include <ddk/protocol/usb_hub.h>
#include <magenta/types.h>
#include <runtime/mutex.h>
#include <pthread.h>
#include <stdio.h>

#include "usb_poll.h"

#ifdef XHCI_DEBUG
#define xhci_debug(fmt, args...) printf("%s: " fmt, __func__, ##args)
#else
#define xhci_debug(fmt, args...) \
    do {                         \
    } while (0)
#endif
#ifdef XHCI_SPEW_DEBUG
#define xhci_spew(fmt, args...) xhci_debug(fmt, ##args)
#else
#define xhci_spew(fmt, args...) \
    do {                        \
    } while (0)
#endif

#define MASK(startbit, lenbit) (((1 << (lenbit)) - 1) << (startbit))

/* Make these high enough to not collide with negative XHCI CCs */
#define TIMEOUT -65
#define CONTROLLER_ERROR -66
#define COMMUNICATION_ERROR -67
#define OUT_OF_MEMORY -68
#define DRIVER_ERROR -69

#define CC_SUCCESS 1
#define CC_TRB_ERROR 5
#define CC_STALL_ERROR 6
#define CC_RESOURCE_ERROR 7
#define CC_BANDWIDTH_ERROR 8
#define CC_NO_SLOTS_AVAILABLE 9
#define CC_SHORT_PACKET 13
#define CC_EVENT_RING_FULL_ERROR 21
#define CC_COMMAND_RING_STOPPED 24
#define CC_COMMAND_ABORTED 25
#define CC_STOPPED 26
#define CC_STOPPED_LENGTH_INVALID 27

enum {
    TRB_NORMAL = 1,
    TRB_SETUP_STAGE = 2,
    TRB_DATA_STAGE = 3,
    TRB_STATUS_STAGE = 4,
    TRB_LINK = 6,
    TRB_EVENT_DATA = 7,
    TRB_CMD_ENABLE_SLOT = 9,
    TRB_CMD_DISABLE_SLOT = 10,
    TRB_CMD_ADDRESS_DEV = 11,
    TRB_CMD_CONFIGURE_EP = 12,
    TRB_CMD_EVAL_CTX = 13,
    TRB_CMD_RESET_EP = 14,
    TRB_CMD_STOP_EP = 15,
    TRB_CMD_SET_TR_DQ = 16,
    TRB_CMD_NOOP = 23,
    TRB_EV_TRANSFER = 32,
    TRB_EV_CMD_CMPL = 33,
    TRB_EV_PORTSC = 34,
    TRB_EV_HOST = 37,
};
enum { TRB_TRT_NO_DATA = 0,
       TRB_TRT_OUT_DATA = 2,
       TRB_TRT_IN_DATA = 3 };
enum { TRB_DIR_OUT = 0,
       TRB_DIR_IN = 1 };

#define TRB_PORT_FIELD ptr_low
#define TRB_PORT_START 24
#define TRB_PORT_LEN 8
#define TRB_TL_FIELD status /* TL - Transfer Length */
#define TRB_TL_START 0
#define TRB_TL_LEN 17
#define TRB_EVTL_FIELD status /* EVTL - (Event TRB) Transfer Length */
#define TRB_EVTL_START 0
#define TRB_EVTL_LEN 24
#define TRB_TDS_FIELD status /* TDS - TD Size */
#define TRB_TDS_START 17
#define TRB_TDS_LEN 5
#define TRB_CC_FIELD status /* CC - Completion Code */
#define TRB_CC_START 24
#define TRB_CC_LEN 8
#define TRB_C_FIELD control /* C - Cycle Bit */
#define TRB_C_START 0
#define TRB_C_LEN 1
#define TRB_TC_FIELD control /* TC - Toggle Cycle */
#define TRB_TC_START 1
#define TRB_TC_LEN 1
#define TRB_ENT_FIELD control /* ENT - Evaluate Next TRB */
#define TRB_ENT_START 1
#define TRB_ENT_LEN 1
#define TRB_ISP_FIELD control /* ISP - Interrupt-on Short Packet */
#define TRB_ISP_START 2
#define TRB_ISP_LEN 1
#define TRB_CH_FIELD control /* CH - Chain Bit */
#define TRB_CH_START 4
#define TRB_CH_LEN 1
#define TRB_IOC_FIELD control /* IOC - Interrupt On Completion */
#define TRB_IOC_START 5
#define TRB_IOC_LEN 1
#define TRB_IDT_FIELD control /* IDT - Immediate Data */
#define TRB_IDT_START 6
#define TRB_IDT_LEN 1
#define TRB_DC_FIELD control /* DC - Deconfigure */
#define TRB_DC_START 9
#define TRB_DC_LEN 1
#define TRB_TT_FIELD control /* TT - TRB Type */
#define TRB_TT_START 10
#define TRB_TT_LEN 6
#define TRB_TRT_FIELD control /* TRT - Transfer Type */
#define TRB_TRT_START 16
#define TRB_TRT_LEN 2
#define TRB_DIR_FIELD control /* DIR - Direction */
#define TRB_DIR_START 16
#define TRB_DIR_LEN 1
#define TRB_EP_FIELD control /* EP - Endpoint ID */
#define TRB_EP_START 16
#define TRB_EP_LEN 5
#define TRB_ID_FIELD control /* ID - Slot ID */
#define TRB_ID_START 24
#define TRB_ID_LEN 8
#define TRB_MASK(tok) MASK(TRB_##tok##_START, TRB_##tok##_LEN)
#define TRB_GET(tok, trb) (((trb)->TRB_##tok##_FIELD & TRB_MASK(tok)) >> TRB_##tok##_START)
#define TRB_SET(tok, trb, to) (trb)->TRB_##tok##_FIELD =                         \
                                  (((trb)->TRB_##tok##_FIELD & ~TRB_MASK(tok)) | \
                                   (((to) << TRB_##tok##_START) & TRB_MASK(tok)))
#define TRB_DUMP(tok, trb) xhci_debug(" " #tok "\t0x%04" PRIx32 "\n", TRB_GET(tok, trb))

#define TRB_CYCLE (1 << 0)
typedef volatile struct trb {
    uint32_t ptr_low;
    uint32_t ptr_high;
    uint32_t status;
    uint32_t control;
} trb_t;

#define TRB_MAX_TD_SIZE 0x1F /* bits 21:17 of TD Size in TRB */

#define EVENT_RING_SIZE 64
typedef struct {
    trb_t* ring;
    trb_t* cur;
    trb_t* last;
    uint8_t ccs;
    uint8_t adv;
} event_ring_t;

/* Never raise this above 256 to prevent transfer event length overflow! */
#define TRANSFER_RING_SIZE 32
typedef struct {
    trb_t* ring;
    trb_t* cur;
    uint8_t pcs;
} __attribute__((packed)) transfer_ring_t;

#define COMMAND_RING_SIZE 4
typedef transfer_ring_t command_ring_t;

#define SC_ROUTE_FIELD f1 /* ROUTE - Route String */
#define SC_ROUTE_START 0
#define SC_ROUTE_LEN 20
#define SC_SPEED1_FIELD f1 /* SPEED - Port speed plus one (compared to usb_speed enum) */
#define SC_SPEED1_START 20
#define SC_SPEED1_LEN 4
#define SC_MTT_FIELD f1 /* MTT - Multi Transaction Translator */
#define SC_MTT_START 25
#define SC_MTT_LEN 1
#define SC_HUB_FIELD f1 /* HUB - Is this a hub? */
#define SC_HUB_START 26
#define SC_HUB_LEN 1
#define SC_CTXENT_FIELD f1 /* CTXENT - Context Entries (number of following ep contexts) */
#define SC_CTXENT_START 27
#define SC_CTXENT_LEN 5
#define SC_RHPORT_FIELD f2 /* RHPORT - Root Hub Port Number */
#define SC_RHPORT_START 16
#define SC_RHPORT_LEN 8
#define SC_NPORTS_FIELD f2 /* NPORTS - Number of Ports */
#define SC_NPORTS_START 24
#define SC_NPORTS_LEN 8
#define SC_TTID_FIELD f3 /* TTID - TT Hub Slot ID */
#define SC_TTID_START 0
#define SC_TTID_LEN 8
#define SC_TTPORT_FIELD f3 /* TTPORT - TT Port Number */
#define SC_TTPORT_START 8
#define SC_TTPORT_LEN 8
#define SC_TTT_FIELD f3 /* TTT - TT Think Time */
#define SC_TTT_START 16
#define SC_TTT_LEN 2
#define SC_UADDR_FIELD f4 /* UADDR - USB Device Address */
#define SC_UADDR_START 0
#define SC_UADDR_LEN 8
#define SC_STATE_FIELD f4 /* STATE - Slot State */
#define SC_STATE_START 27
#define SC_STATE_LEN 8
#define SC_MASK(tok) MASK(SC_##tok##_START, SC_##tok##_LEN)
#define SC_GET(tok, sc) (((sc)->SC_##tok##_FIELD & SC_MASK(tok)) >> SC_##tok##_START)
#define SC_SET(tok, sc, to) (sc)->SC_##tok##_FIELD =                        \
                                (((sc)->SC_##tok##_FIELD & ~SC_MASK(tok)) | \
                                 (((to) << SC_##tok##_START) & SC_MASK(tok)))
#define SC_DUMP(tok, sc) xhci_debug(" " #tok "\t0x%04" PRIx32 "\n", SC_GET(tok, sc))
typedef volatile struct slotctx {
    uint32_t f1;
    uint32_t f2;
    uint32_t f3;
    uint32_t f4;
    uint32_t rsvd[4];
} slotctx_t;

#define EC_STATE_FIELD f1 /* STATE - Endpoint State */
#define EC_STATE_START 0
#define EC_STATE_LEN 3
#define EC_INTVAL_FIELD f1 /* INTVAL - Interval */
#define EC_INTVAL_START 16
#define EC_INTVAL_LEN 8
#define EC_CERR_FIELD f2 /* CERR - Error Count */
#define EC_CERR_START 1
#define EC_CERR_LEN 2
#define EC_TYPE_FIELD f2 /* TYPE - EP Type */
#define EC_TYPE_START 3
#define EC_TYPE_LEN 3
#define EC_MBS_FIELD f2 /* MBS - Max Burst Size */
#define EC_MBS_START 8
#define EC_MBS_LEN 8
#define EC_MPS_FIELD f2 /* MPS - Max Packet Size */
#define EC_MPS_START 16
#define EC_MPS_LEN 16
#define EC_DCS_FIELD tr_dq_low /* DCS - Dequeue Cycle State */
#define EC_DCS_START 0
#define EC_DCS_LEN 1
#define EC_AVRTRB_FIELD f5 /* AVRTRB - Average TRB Length */
#define EC_AVRTRB_START 0
#define EC_AVRTRB_LEN 16
#define EC_MXESIT_FIELD f5 /* MXESIT - Max ESIT Payload */
#define EC_MXESIT_START 16
#define EC_MXESIT_LEN 16
#define EC_BPKTS_FIELD rsvd[0] /* BPKTS - packets tx in scheduled uframe */
#define EC_BPKTS_START 0
#define EC_BPKTS_LEN 6
#define EC_BBM_FIELD rsvd[0] /* BBM - burst mode for scheduling */
#define EC_BBM_START 11
#define EC_BBM_LEN 1

#define EC_MASK(tok) MASK(EC_##tok##_START, EC_##tok##_LEN)
#define EC_GET(tok, ec) (((ec)->EC_##tok##_FIELD & EC_MASK(tok)) >> EC_##tok##_START)
#define EC_SET(tok, ec, to) (ec)->EC_##tok##_FIELD =                        \
                                (((ec)->EC_##tok##_FIELD & ~EC_MASK(tok)) | \
                                 (((to) << EC_##tok##_START) & EC_MASK(tok)))
#define EC_DUMP(tok, ec) xhci_debug(" " #tok "\t0x%04" PRIx32 "\n", EC_GET(tok, ec))
enum { EP_ISOC_OUT = 1,
       EP_BULK_OUT = 2,
       EP_INTR_OUT = 3,
       EP_CONTROL = 4,
       EP_ISOC_IN = 5,
       EP_BULK_IN = 6,
       EP_INTR_IN = 7 };
typedef volatile struct epctx {
    uint32_t f1;
    uint32_t f2;
    uint32_t tr_dq_low;
    uint32_t tr_dq_high;
    uint32_t f5;
    uint32_t rsvd[3];
} epctx_t;

#define NUM_EPS 32
#define CTXSIZE(xhci) ((xhci)->capreg->csz ? 64 : 32)

typedef struct usbdev {
    int num_endp;
    usb_endpoint_t ep0;
    int address; // usb address
    int hub;     // hub, device is attached to
    int port;    // port where device is attached
    usb_speed speed;
    struct usb_xhci* hci;

    list_node_t req_queue;
} usbdev_t;

typedef union devctx {
    /* set of pointers, so we can dynamically adjust Slot/EP context size */
    struct {
        union {
            slotctx_t* slot;
            void* raw; /* Pointer to the whole dev context. */
        };
        epctx_t* ep0;
        epctx_t* eps1_30[NUM_EPS - 2];
    };
    epctx_t* ep[NUM_EPS]; /* At index 0 it's actually the slotctx,
					we have it like that so we can use
					the ep_id directly as index. */
} devctx_t;

typedef struct inputctx {
    union {             /* The drop flags are located at the start of the */
        uint32_t* drop; /* structure, so a pointer to them is equivalent */
        void* raw;      /* to a pointer to the whole (raw) input context. */
    };
    uint32_t* add;
    devctx_t dev;
} inputctx_t;

typedef struct devinfo {
    devctx_t ctx;
    transfer_ring_t* transfer_rings[NUM_EPS];
} devinfo_t;

typedef struct erst_entry {
    uint32_t seg_base_lo;
    uint32_t seg_base_hi;
    uint32_t seg_size;
    uint32_t rsvd;
} erst_entry_t;

typedef struct xhci {
    /* capreg is read-only, so no need for volatile,
	   and thus 32bit accesses can be assumed. */
    struct capreg {
        uint8_t caplength;
        uint8_t res1;
        union {
            uint16_t hciversion;
            struct {
                uint8_t hciver_lo;
                uint8_t hciver_hi;
            } __attribute__((packed));
        } __attribute__((packed));
        union {
            uint32_t hcsparams1;
            struct {
                unsigned long MaxSlots : 7;
                unsigned long MaxIntrs : 11;
                unsigned long : 6;
                unsigned long MaxPorts : 8;
            } __attribute__((packed));
        } __attribute__((packed));
        union {
            uint32_t hcsparams2;
            struct {
                unsigned long IST : 4;
                unsigned long ERST_Max : 4;
                unsigned long : 13;
                unsigned long Max_Scratchpad_Bufs_Hi : 5;
                unsigned long SPR : 1;
                unsigned long Max_Scratchpad_Bufs_Lo : 5;
            } __attribute__((packed));
        } __attribute__((packed));
        union {
            uint32_t hcsparams3;
            struct {
                unsigned long u1latency : 8;
                unsigned long : 8;
                unsigned long u2latency : 16;
            } __attribute__((packed));
        } __attribute__((packed));
        union {
            uint32_t hccparams;
            struct {
                unsigned long ac64 : 1;
                unsigned long bnc : 1;
                unsigned long csz : 1;
                unsigned long ppc : 1;
                unsigned long pind : 1;
                unsigned long lhrc : 1;
                unsigned long ltc : 1;
                unsigned long nss : 1;
                unsigned long : 4;
                unsigned long MaxPSASize : 4;
                unsigned long xECP : 16;
            } __attribute__((packed));
        } __attribute__((packed));
        uint32_t dboff;
        uint32_t rtsoff;
    } __attribute__((packed)) * capreg;

    /* opreg is R/W is most places, so volatile access is necessary.
	   volatile means that the compiler seeks byte writes if possible,
	   making bitfields unusable for MMIO register blocks. Yay C :-( */
    volatile struct opreg {
        uint32_t usbcmd;
#define USBCMD_RS 1 << 0
#define USBCMD_HCRST 1 << 1
#define USBCMD_INTE 1 << 2
        uint32_t usbsts;
#define USBSTS_HCH 1 << 0
#define USBSTS_HSE 1 << 2
#define USBSTS_EINT 1 << 3
#define USBSTS_PCD 1 << 4
#define USBSTS_CNR 1 << 11
#define USBSTS_PRSRV_MASK ((1 << 1) | 0xffffe000)
        uint32_t pagesize;
        uint8_t res1[0x13 - 0x0c + 1];
        uint32_t dnctrl;
        uint32_t crcr_lo;
        uint32_t crcr_hi;
#define CRCR_RCS 1 << 0
#define CRCR_CS 1 << 1
#define CRCR_CA 1 << 2
#define CRCR_CRR 1 << 3
        uint8_t res2[0x2f - 0x20 + 1];
        uint32_t dcbaap_lo;
        uint32_t dcbaap_hi;
        uint32_t config;
#define CONFIG_LP_MASK_MaxSlotsEn 0xff
        uint8_t res3[0x3ff - 0x3c + 1];
        struct {
            uint32_t portsc;
#define PORTSC_CCS (1 << 0)
#define PORTSC_PED (1 << 1)
// BIT 2 rsvdZ
#define PORTSC_OCA (1 << 3)
#define PORTSC_PR (1 << 4)
#define PORTSC_PLS (1 << 5)
#define PORTSC_PLS_MASK MASK(5, 4)
#define PORTSC_PP (1 << 9)
#define PORTSC_PORT_SPEED_START 10
#define PORTSC_PORT_SPEED (1 << PORTSC_PORT_SPEED_START)
#define PORTSC_PORT_SPEED_MASK MASK(PORTSC_PORT_SPEED_START, 4)
#define PORTSC_PIC (1 << 14)
#define PORTSC_PIC_MASK MASK(14, 2)
#define PORTSC_LWS (1 << 16)
#define PORTSC_CSC (1 << 17)
#define PORTSC_PEC (1 << 18)
#define PORTSC_WRC (1 << 19)
#define PORTSC_OCC (1 << 20)
#define PORTSC_PRC (1 << 21)
#define PORTSC_PLC (1 << 22)
#define PORTSC_CEC (1 << 23)
#define PORTSC_CAS (1 << 24)
#define PORTSC_WCE (1 << 25)
#define PORTSC_WDE (1 << 26)
#define PORTSC_WOE (1 << 27)
// BIT 29:28 rsvdZ
#define PORTSC_DR (1 << 30)
#define PORTSC_WPR (1 << 31)
#define PORTSC_RW_MASK (PORTSC_PR | PORTSC_PLS_MASK | PORTSC_PP | PORTSC_PIC_MASK | PORTSC_LWS | PORTSC_WCE | PORTSC_WDE | PORTSC_WOE)
            uint32_t portpmsc;
            uint32_t portli;
            uint32_t res;
        } __attribute__((packed)) prs[];
    } __attribute__((packed)) * opreg;

    /* R/W, volatile, MMIO -> no bitfields */
    volatile struct hcrreg {
        uint32_t mfindex;
        uint8_t res1[0x20 - 0x4];
        struct {
            uint32_t iman;
#define IMAN_IP (1 << 0)
#define IMAN_IE (1 << 1)
            uint32_t imod;
            uint32_t erstsz;
            uint32_t res;
            uint32_t erstba_lo;
            uint32_t erstba_hi;
            uint32_t erdp_lo;
            uint32_t erdp_hi;
        } __attribute__((packed)) intrrs[]; // up to 1024, but maximum host specific, given in capreg->MaxIntrs
    } __attribute__((packed)) * hcrreg;

    /* R/W, volatile, MMIO -> no bitfields */
    volatile uint32_t* dbreg;

    /* R/W, volatile, Memory -> bitfields allowed */
    uint64_t* dcbaa;   /* pointers to sp_ptrs and output (device) contexts */
    uint64_t* sp_ptrs; /* pointers to scratchpad buffers */

    command_ring_t cr;
    event_ring_t er;
    volatile erst_entry_t* ev_ring_table;

    usbdev_t* roothub;

    uint8_t max_slots_en;
    devinfo_t* dev; /* array of devinfos by slot_id */

    io_alloc_t* io_alloc;
    uint8_t* ep0_buffer;

    usbdev_t* devices[128]; // dev 0 is root hub, 127 is last addressable

    poll_node_t poll_node;

    list_node_t completed_reqs;

    mx_device_t* bus_device;
    usb_bus_protocol_t* bus_protocol;
    int num_rh_ports;

    mxr_mutex_t mutex;
} xhci_t;

typedef struct usb_xhci {
    xhci_t xhci;
    mx_device_t hcidev; // HCI device

    io_alloc_t* io_alloc;
    void* mmio;
    uint64_t mmio_len;

    pci_protocol_t* pci;
    mx_handle_t irq_handle;
    mx_handle_t mmio_handle;
    mx_handle_t cfg_handle;
    pthread_t irq_thread;
} usb_xhci_t;

mx_status_t xhci_startup(usb_xhci_t* uxhci);

#define get_usb_xhci(dev) (containerof(dev, usb_xhci_t, hcidev))
#define get_xhci(dev) (&get_usb_xhci(dev)->xhci)

mx_status_t xhci_rh_init(usb_xhci_t* uxhci);
void xhci_rh_check_status_changed(xhci_t* xhci);

void* xhci_align(xhci_t* xhci, const size_t min_align, const size_t size);
void xhci_init_cycle_ring(xhci_t* const xhci, transfer_ring_t*, const size_t ring_size);
int xhci_set_address(mx_device_t* hcidev, usb_speed speed, int hubport, int hubaddr);
int xhci_finish_device_config(mx_device_t* hcidev, int devaddr, usb_device_config_t* config);
void xhci_destroy_dev(mx_device_t* hcidev, int slot_id);

void xhci_reset_event_ring(event_ring_t*);
void xhci_advance_event_ring(xhci_t*);
void xhci_update_event_dq(xhci_t*);

// must hold mutex when calling this
void xhci_handle_events(xhci_t* xhci);

int xhci_wait_for_command_aborted(xhci_t*, const trb_t*);
int xhci_wait_for_command_done(xhci_t*, const trb_t*, int clear_event);
int xhci_wait_for_transfer(xhci_t*, uint32_t slot_id, uint32_t ep_id);

void xhci_clear_trb(trb_t*, int pcs);

trb_t* xhci_next_command_trb(xhci_t*);
void xhci_post_command(xhci_t*);
int xhci_cmd_enable_slot(xhci_t*, int* slot_id);
int xhci_cmd_disable_slot(xhci_t*, int slot_id);
int xhci_cmd_address_device(xhci_t*, int slot_id, inputctx_t*);
int xhci_cmd_configure_endpoint(xhci_t*, int slot_id, int config_id, inputctx_t*);
int xhci_cmd_evaluate_context(xhci_t*, int slot_id, inputctx_t*);
int xhci_cmd_reset_endpoint(xhci_t*, int slot_id, int ep);
int xhci_cmd_stop_endpoint(xhci_t*, int slot_id, int ep);
int xhci_cmd_set_tr_dq(xhci_t*, int slot_id, int ep, trb_t*, int dcs);

static inline int xhci_ep_id(const usb_endpoint_t* const ep) {
    return ((ep->endpoint & 0x7f) << 1) + (ep->direction == USB_ENDPOINT_IN);
}

mx_paddr_t xhci_virt_to_phys(xhci_t* const xhci, mx_vaddr_t addr);
mx_vaddr_t xhci_phys_to_virt(xhci_t* const xhci, mx_paddr_t addr);
void* xhci_malloc(xhci_t* const xhci, size_t size);
void* xhci_memalign(xhci_t* const xhci, size_t alignment, size_t size);
void xhci_free(xhci_t* const xhci, void* addr);
void xhci_free_phys(xhci_t* const xhci, mx_paddr_t addr);

usbdev_t* init_device_entry(usb_xhci_t* hci, int i);

int xhci_get_descriptor(usbdev_t* dev, int rtype, int desc_type, int desc_idx,
                        void* data, size_t len);

usb_hci_protocol_t _xhci_protocol;
usb_hub_protocol_t xhci_rh_hub_protocol;

#if ARCH_X86_32 || ARCH_X86_64
#define wmb() __asm__ volatile("sfence")
#else
// FIXME (voydanoff)
#define wmb()
#endif

#define MIN(x, y) (x < y ? x : y)

#ifdef XHCI_DUMPS
void xhci_dump_slotctx(const slotctx_t*);
void xhci_dump_epctx(const epctx_t*);
void xhci_dump_devctx(const devctx_t*, const uint32_t ctx_mask);
void xhci_dump_inputctx(const inputctx_t*);
void xhci_dump_transfer_trb(const trb_t*);
void xhci_dump_transfer_trbs(const trb_t* first, const trb_t* last);
#else
#define xhci_dump_slotctx(args...) \
    do {                           \
    } while (0)
#define xhci_dump_epctx(args...) \
    do {                         \
    } while (0)
#define xhci_dump_devctx(args...) \
    do {                          \
    } while (0)
#define xhci_dump_inputctx(args...) \
    do {                            \
    } while (0)
#define xhci_dump_transfer_trb(args...) \
    do {                                \
    } while (0)
#define xhci_dump_transfer_trbs(args...) \
    do {                                 \
    } while (0)
#endif
