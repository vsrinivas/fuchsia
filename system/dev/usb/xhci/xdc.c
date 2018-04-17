// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <zircon/hw/usb.h>
#include <assert.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "xdc.h"
#include "xdc-hw.h"
#include "xhci-hw.h"
#include "xhci-trb.h"
#include "xhci-util.h"

// The type and length fields for a string descriptor are one byte each.
#define STR_DESC_METADATA_LEN  2
#define MAX_STR_LEN            64
// String descriptors use UNICODE UTF-16LE encodings.
#define XDC_MANUFACTURER       u"Google Inc."
#define XDC_PRODUCT            u"Fuchsia XDC Target"
#define XDC_SERIAL_NUMBER      u""
#define XDC_VENDOR_ID          0x18D1
#define XDC_PRODUCT_ID         0xA0DC
#define XDC_REVISION           0x1000

// Multi-segment event rings are not currently supported.
#define ERST_ARRAY_SIZE        1
#define EVENT_RING_SIZE        (PAGE_SIZE / sizeof(xhci_trb_t))
#define TRANSFER_RING_SIZE     (PAGE_SIZE / sizeof(xhci_trb_t))

// See XHCI Spec, 7.6.3.2
#define EP_CTX_MAX_PACKET_SIZE 1024

// There are only two endpoints, one for bulk OUT and one for bulk IN.
#define OUT_EP_IDX             0
#define IN_EP_IDX              1
#define NUM_EPS                2

typedef struct {
    xhci_transfer_ring_t transfer_ring;
} xdc_endpoint_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint8_t string[MAX_STR_LEN];
} xdc_str_desc_t;

typedef struct {
    xdc_str_desc_t str_0_desc;
    xdc_str_desc_t manufacturer_desc;
    xdc_str_desc_t product_desc;
    xdc_str_desc_t serial_num_desc;
} xdc_str_descs_t;

typedef struct {
    zx_device_t* zxdev;

    // Shared from XHCI.
    zx_handle_t bti_handle;
    void* mmio;

    xdc_debug_cap_regs_t* debug_cap_regs;

    // Underlying buffer for the event ring segment table
    io_buffer_t erst_buffer;
    erst_entry_t* erst_array;

    xhci_event_ring_t event_ring;

    // Underlying buffer for the context data and string descriptors.
    io_buffer_t context_str_descs_buffer;
    xdc_context_data_t* context_data;
    xdc_str_descs_t* str_descs;

    xdc_endpoint_t eps[NUM_EPS];

    thrd_t start_thread;
} xdc_t;

static void xdc_wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected) {
    uint32_t value = XHCI_READ32(ptr);
    while ((value & bits) != expected) {
        usleep(1000);
        value = XHCI_READ32(ptr);
    }
}

// Populates the pointer to the debug capability in the xdc struct.
static zx_status_t xdc_get_debug_cap(xdc_t* xdc) {
    uint32_t cap_id = EXT_CAP_USB_DEBUG_CAPABILITY;
    xdc->debug_cap_regs = (xdc_debug_cap_regs_t*)xhci_get_next_ext_cap(xdc->mmio, NULL, &cap_id);
    return xdc->debug_cap_regs ? ZX_OK : ZX_ERR_NOT_FOUND;
}

// Populates the string descriptors and info context (DbCIC) string descriptor metadata.
static void xdc_str_descs_init(xdc_t* xdc, zx_paddr_t strs_base) {
    xdc_str_descs_t* strs = xdc->str_descs;

    // String Descriptor 0 contains the supported languages as a list of numbers (LANGIDs).
    // 0x0409: English (United States)
    strs->str_0_desc.string[0] = 0x09;
    strs->str_0_desc.string[1] = 0x04;
    strs->str_0_desc.len = STR_DESC_METADATA_LEN + 2;
    strs->str_0_desc.type = USB_DT_STRING;

    memcpy(&strs->manufacturer_desc.string, XDC_MANUFACTURER, sizeof(XDC_MANUFACTURER));
    strs->manufacturer_desc.len = STR_DESC_METADATA_LEN + sizeof(XDC_MANUFACTURER);
    strs->manufacturer_desc.type = USB_DT_STRING;

    memcpy(&strs->product_desc.string, XDC_PRODUCT, sizeof(XDC_PRODUCT));
    strs->product_desc.len = STR_DESC_METADATA_LEN + sizeof(XDC_PRODUCT);
    strs->product_desc.type = USB_DT_STRING;

    memcpy(&strs->serial_num_desc.string, XDC_SERIAL_NUMBER, sizeof(XDC_SERIAL_NUMBER));
    strs->serial_num_desc.len = STR_DESC_METADATA_LEN + sizeof(XDC_SERIAL_NUMBER);
    strs->serial_num_desc.type = USB_DT_STRING;

    // Populate the addresses and lengths of the string descriptors in the info context (DbCIC).
    xdc_dbcic_t* dbcic = &xdc->context_data->dbcic;

    dbcic->str_0_desc_addr = strs_base + offsetof(xdc_str_descs_t, str_0_desc);
    dbcic->manufacturer_desc_addr = strs_base + offsetof(xdc_str_descs_t, manufacturer_desc);
    dbcic->product_desc_addr = strs_base + offsetof(xdc_str_descs_t, product_desc);
    dbcic->serial_num_desc_addr = strs_base + offsetof(xdc_str_descs_t, serial_num_desc);

    dbcic->str_0_desc_len = strs->str_0_desc.len;
    dbcic->manufacturer_desc_len = strs->manufacturer_desc.len;
    dbcic->product_desc_len = strs->product_desc.len;
    dbcic->serial_num_desc_len = strs->serial_num_desc.len;
}

static zx_status_t xdc_endpoint_ctx_init(xdc_t* xdc, uint32_t ep_idx) {
    if (ep_idx >= NUM_EPS) {
        return ZX_ERR_INVALID_ARGS;
    }
    xdc_endpoint_t* ep = &xdc->eps[ep_idx];
    zx_status_t status = xhci_transfer_ring_init(&ep->transfer_ring, xdc->bti_handle,
                                                 TRANSFER_RING_SIZE);
    if (status != ZX_OK) {
        return status;
    }
    zx_paddr_t tr_dequeue = xhci_transfer_ring_start_phys(&ep->transfer_ring);

    uint32_t max_burst = XHCI_GET_BITS32(&xdc->debug_cap_regs->dcctrl,
                                         DCCTRL_MAX_BURST_START, DCCTRL_MAX_BURST_BITS);
    int avg_trb_length = EP_CTX_MAX_PACKET_SIZE * (max_burst + 1);


    xhci_endpoint_context_t* epc =
        ep_idx == IN_EP_IDX ? &xdc->context_data->in_epc : &xdc->context_data->out_epc;

    XHCI_WRITE32(&epc->epc0, 0);

    XHCI_SET_BITS32(&epc->epc1, EP_CTX_EP_TYPE_START, EP_CTX_EP_TYPE_BITS,
                    ep_idx == IN_EP_IDX ? EP_CTX_EP_TYPE_BULK_IN : EP_CTX_EP_TYPE_BULK_OUT);
    XHCI_SET_BITS32(&epc->epc1, EP_CTX_MAX_BURST_SIZE_START, EP_CTX_MAX_BURST_SIZE_BITS,
                    max_burst);
    XHCI_SET_BITS32(&epc->epc1, EP_CTX_MAX_PACKET_SIZE_START, EP_CTX_MAX_PACKET_SIZE_BITS,
                    EP_CTX_MAX_PACKET_SIZE);

    XHCI_WRITE32(&epc->epc2, ((uint32_t)tr_dequeue & EP_CTX_TR_DEQUEUE_LO_MASK) | EP_CTX_DCS);
    XHCI_WRITE32(&epc->tr_dequeue_hi, (uint32_t)(tr_dequeue >> 32));

    XHCI_SET_BITS32(&epc->epc4, EP_CTX_AVG_TRB_LENGTH_START, EP_CTX_AVG_TRB_LENGTH_BITS,
                    avg_trb_length);
    // The Endpoint Context Interval, LSA, MaxPStreams, Mult, HID, Cerr, FE and
    // Max Esit Payload fields do not apply to the DbC. See section 7.6.3.2 of XHCI Spec.
    return ZX_OK;
}

static zx_status_t xdc_context_data_init(xdc_t* xdc) {
    // Allocate a buffer to store the context data and string descriptors.
    zx_status_t status = io_buffer_init(&xdc->context_str_descs_buffer,
                                        xdc->bti_handle, PAGE_SIZE,
                                        IO_BUFFER_RW | IO_BUFFER_CONTIG | IO_BUFFER_UNCACHED);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to alloc xdc context and strings buffer, err: %d\n", status);
        return status;
    }
    xdc->context_data = (xdc_context_data_t *)io_buffer_virt(&xdc->context_str_descs_buffer);
    zx_paddr_t context_data_phys = io_buffer_phys(&xdc->context_str_descs_buffer);

    // The context data only takes 192 bytes, so we can store the string descriptors after it.
    xdc->str_descs = (void *)xdc->context_data + sizeof(xdc_context_data_t);
    zx_paddr_t str_descs_phys = context_data_phys + sizeof(xdc_context_data_t);

    // Populate the string descriptors, and string descriptor metadata in the context data.
    xdc_str_descs_init(xdc, str_descs_phys);

    // Initialize the endpoint contexts in the context data.
    for (uint32_t i = 0; i < NUM_EPS; i++) {
        status = xdc_endpoint_ctx_init(xdc, i);
        if (status != ZX_OK) {
            return status;
        }
    }
    XHCI_WRITE64(&xdc->debug_cap_regs->dccp, context_data_phys);
    return ZX_OK;
}

// Updates the event ring dequeue pointer register to the current event ring position.
static void xdc_update_erdp(xdc_t* xdc) {
    uint64_t erdp = xhci_event_ring_current_phys(&xdc->event_ring);
    XHCI_WRITE64(&xdc->debug_cap_regs->dcerdp, erdp);
}

// Sets up the event ring segment table and buffers.
static zx_status_t xdc_event_ring_init(xdc_t* xdc) {
    // Event Ring Segment Table and Event Ring Segments
    zx_status_t status = io_buffer_init(&xdc->erst_buffer, xdc->bti_handle, PAGE_SIZE,
                                        IO_BUFFER_RW | IO_BUFFER_CONTIG | IO_BUFFER_UNCACHED);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to alloc xdc erst_buffer, err: %d\n", status);
        return status;
    }

    xdc->erst_array = (erst_entry_t *)io_buffer_virt(&xdc->erst_buffer);
    zx_paddr_t erst_array_phys = io_buffer_phys(&xdc->erst_buffer);

    status = xhci_event_ring_init(&xdc->event_ring, xdc->bti_handle,
                                  xdc->erst_array, EVENT_RING_SIZE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "xhci_event_ring_init failed, err: %d\n", status);
        return status;
    }

    // Update the event ring dequeue pointer.
    xdc_update_erdp(xdc);

    XHCI_SET32(&xdc->debug_cap_regs->dcerstsz, ERSTSZ_MASK, ERST_ARRAY_SIZE);
    XHCI_WRITE64(&xdc->debug_cap_regs->dcerstba, erst_array_phys);

    return ZX_OK;
}

static zx_status_t xdc_init(xdc_t* xdc) {
    // Initialize the Device Descriptor Info Registers.
    XHCI_WRITE32(&xdc->debug_cap_regs->dcddi1, XDC_VENDOR_ID << DCDDI1_VENDOR_ID_START);
    XHCI_WRITE32(&xdc->debug_cap_regs->dcddi2,
                 (XDC_REVISION << DCDDI2_DEVICE_REVISION_START) | XDC_PRODUCT_ID);

    zx_status_t status = xdc_event_ring_init(xdc);
    if (status != ZX_OK) {
        return status;
    }
    status = xdc_context_data_init(xdc);
    if (status != ZX_OK) {
        return status;
    }
    return ZX_OK;
}

static void xdc_shutdown(xdc_t* xdc) {
    zxlogf(TRACE, "xdc_shutdown\n");

    int res;
    thrd_join(xdc->start_thread, &res);
    if (res != 0) {
        zxlogf(ERROR, "failed to join with xdc start_thread\n");
    }

    XHCI_WRITE32(&xdc->debug_cap_regs->dcctrl, 0);
    xdc_wait_bits(&xdc->debug_cap_regs->dcctrl, DCCTRL_DCR, 0);

    zxlogf(TRACE, "xdc_shutdown succeeded\n");
}

static void xdc_free(xdc_t* xdc) {
    zxlogf(INFO, "xdc_free\n");

    io_buffer_release(&xdc->erst_buffer);
    io_buffer_release(&xdc->context_str_descs_buffer);

    xhci_event_ring_free(&xdc->event_ring);

    for (uint32_t i = 0; i < NUM_EPS; ++i) {
        xdc_endpoint_t* ep = &xdc->eps[i];
        xhci_transfer_ring_free(&ep->transfer_ring);
    }
    free(xdc);
}

static zx_status_t xdc_suspend(void* ctx, uint32_t flags) {
    zxlogf(TRACE, "xdc_suspend %u\n", flags);
    xdc_t* xdc = ctx;

    // TODO(jocelyndang) do different things based on the flags.
    // For now we shutdown the driver in preparation for mexec.
    xdc_shutdown(xdc);

    return ZX_OK;
}

static void xdc_unbind(void* ctx) {
    zxlogf(INFO, "xdc_unbind\n");
    xdc_t* xdc = ctx;
    xdc_shutdown(xdc);
    device_remove(xdc->zxdev);
}

static void xdc_release(void* ctx) {
    zxlogf(INFO, "xdc_release\n");
    xdc_t* xdc = ctx;
    xdc_free(xdc);
}

static zx_protocol_device_t xdc_proto = {
    .version = DEVICE_OPS_VERSION,
    .suspend = xdc_suspend,
    .unbind = xdc_unbind,
    .release = xdc_release
};

static int xdc_start_thread(void* arg) {
    xdc_t* xdc = arg;

    zxlogf(TRACE, "about to enable XHCI DBC\n");
    XHCI_WRITE32(&xdc->debug_cap_regs->dcctrl, DCCTRL_LSE | DCCTRL_DCE);

    return 0;
}

zx_status_t xdc_bind(zx_device_t* parent, zx_handle_t bti_handle, void* mmio) {
#if !defined(__x86_64__)
    return ZX_ERR_NOT_SUPPORTED;
#endif
    xdc_t* xdc = calloc(1, sizeof(xdc_t));
    if (!xdc) {
        return ZX_ERR_NO_MEMORY;
    }
    xdc->bti_handle = bti_handle;
    xdc->mmio = mmio;

    zx_status_t status = xdc_get_debug_cap(xdc);
    if (status != ZX_OK) {
        zxlogf(ERROR, "xdc_get_debug_cap, err: %d\n", status);
        goto error_return;
    }
    status = xdc_init(xdc);
    if (status != ZX_OK) {
        zxlogf(ERROR, "xdc_init failed, err: %d\n", status);
        goto error_return;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "xdc",
        .ctx = xdc,
        .ops = &xdc_proto,
        .proto_id = ZX_PROTOCOL_USB_DBC,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &xdc->zxdev);
    if (status != ZX_OK) {
        goto error_return;
    }

    int ret = thrd_create_with_name(&xdc->start_thread, xdc_start_thread, xdc, "xdc_start_thread");
    if (ret != thrd_success) {
        device_remove(xdc->zxdev);
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;

error_return:
    zxlogf(ERROR, "xdc_bind failed: %d\n", status);
    xdc_free(xdc);
    return status;
}
