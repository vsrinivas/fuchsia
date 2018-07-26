// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <xdc-server-utils/msg.h>
#include <zircon/device/debug.h>
#include <zircon/hw/usb.h>
#include <assert.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "xdc.h"
#include "xdc-transfer.h"
#include "xhci-hw.h"
#include "xhci-util.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

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

// The maximum duration to transition from connected to configured state.
#define TRANSITION_CONFIGURED_THRESHOLD ZX_SEC(5)

#define OUT_EP_ADDR            0x01
#define IN_EP_ADDR             0x81

#define MAX_REQS               10
#define MAX_REQ_SIZE           4096

typedef struct xdc_instance {
    zx_device_t* zxdev;
    xdc_t* parent;

    // Whether the instance has registered a stream ID.
    bool has_stream_id;
    // ID of stream that this instance is reading and writing from.
    // Only valid if has_stream_id is true.
    uint32_t stream_id;
    // Whether the host has registered a stream of the same id.
    bool connected;
    bool dead;
    xdc_packet_state_t cur_read_packet;
    // Where we've read up to, in the first request of the completed reads list.
    size_t cur_req_read_offset;
    list_node_t completed_reads;
    // Needs to be acquired before accessing the stream_id, dead or read members.
    mtx_t lock;

    // For storing this instance in the parent's instance_list.
    list_node_t node;
} xdc_instance_t;

// For tracking streams registered on the host side.
typedef struct {
    uint32_t stream_id;
    // For storing this in xdc's host_streams list.
    list_node_t node;
} xdc_host_stream_t;

static zx_status_t xdc_write(xdc_t* xdc, uint32_t stream_id, const void* buf, size_t count,
                             size_t* actual, bool is_ctrl_msg);

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
    // Initialize the endpoint.
    xdc_endpoint_t* ep = &xdc->eps[ep_idx];
    list_initialize(&ep->queued_reqs);
    list_initialize(&ep->pending_reqs);
    ep->direction = ep_idx == IN_EP_IDX ? USB_DIR_IN : USB_DIR_OUT;
    snprintf(ep->name, MAX_EP_DEBUG_NAME_LEN, ep_idx == IN_EP_IDX ? "IN" : "OUT");
    ep->state = XDC_EP_STATE_RUNNING;

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

// Initializes the debug capability registers and required data structures.
// This needs to be called everytime the host controller is reset.
static zx_status_t xdc_init_debug_cap(xdc_t* xdc) {
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

static zx_status_t xdc_write_instance(void* ctx, const void* buf, size_t count,
                                      zx_off_t off, size_t* actual) {
    xdc_instance_t* inst = ctx;

    mtx_lock(&inst->lock);

    if (inst->dead) {
        mtx_unlock(&inst->lock);
        return ZX_ERR_PEER_CLOSED;
    }
    if (!inst->has_stream_id) {
        zxlogf(ERROR, "write failed, instance %p did not register for a stream id\n", inst);
        mtx_unlock(&inst->lock);
        return ZX_ERR_BAD_STATE;
    }
    if (!inst->connected) {
        mtx_unlock(&inst->lock);
        return ZX_ERR_SHOULD_WAIT;
    }
    uint32_t stream_id = inst->stream_id;

    mtx_unlock(&inst->lock);

    return xdc_write(inst->parent, stream_id, buf, count, actual, false /* is_ctrl_msg */);
}

static void xdc_update_instance_write_signal(xdc_instance_t* inst, bool writable) {
    mtx_lock(&inst->lock);

    if (inst->dead || !inst->has_stream_id) {
        mtx_unlock(&inst->lock);
        return;
    }

    // For an instance to be writable, we need the xdc device to be ready for writing,
    // and the corresponding stream to be registered on the host.
    if (writable && inst->connected) {
        device_state_set(inst->zxdev, DEV_STATE_WRITABLE);
    } else {
        device_state_clr(inst->zxdev, DEV_STATE_WRITABLE);
    }

    mtx_unlock(&inst->lock);
}

static xdc_host_stream_t* xdc_get_host_stream(xdc_t* xdc, uint32_t stream_id)
                                              __TA_REQUIRES(xdc->instance_list_lock) {
    xdc_host_stream_t* host_stream;
    list_for_every_entry(&xdc->host_streams, host_stream, xdc_host_stream_t, node) {
        if (host_stream->stream_id == stream_id) {
            return host_stream;
        }
    }
    return NULL;
}

// Sends a message to the host to notify when a xdc device stream becomes online or offline.
// If the message cannot be currently sent, it will be queued for later.
static void xdc_notify_stream_state(xdc_t* xdc, uint32_t stream_id, bool online) {
    xdc_msg_t msg = {
        .opcode = XDC_NOTIFY_STREAM_STATE,
        .notify_stream_state = { .stream_id = stream_id, .online = online }
    };

    size_t actual;
    zx_status_t status = xdc_write(xdc, XDC_MSG_STREAM, &msg, sizeof(msg), &actual,
                                   true /* is_ctrl_msg */);
    if (status == ZX_OK) {
        // The write size is much less than the max packet size, so it should complete entirely.
        ZX_DEBUG_ASSERT(actual == sizeof(xdc_msg_t));
    } else {
        // xdc_write should always queue ctrl msgs, unless some fatal error occurs e.g. OOM.
        zxlogf(ERROR, "xdc_write_internal returned err: %d, dropping ctrl msg for stream id %u\n",
               status, stream_id);
    }
}

// Sets the stream id for the device instance.
// Returns ZX_OK if successful, or ZX_ERR_INVALID_ARGS if the stream id is unavailable.
static zx_status_t xdc_register_stream(xdc_instance_t* inst, uint32_t stream_id) {
    xdc_t* xdc = inst->parent;

    if (stream_id == DEBUG_STREAM_ID_RESERVED) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&xdc->instance_list_lock);

    xdc_instance_t* test_inst;
    list_for_every_entry(&xdc->instance_list, test_inst, xdc_instance_t, node) {
        mtx_lock(&test_inst->lock);
        // We can only register the stream id if no one else already has.
        if (test_inst->stream_id == stream_id) {
            zxlogf(ERROR, "stream id %u was already registered\n", stream_id);
            mtx_unlock(&test_inst->lock);
            mtx_unlock(&xdc->instance_list_lock);
            return ZX_ERR_INVALID_ARGS;
        }
        mtx_unlock(&test_inst->lock);
    }

    mtx_lock(&inst->lock);
    inst->stream_id = stream_id;
    inst->has_stream_id = true;
    inst->connected = xdc_get_host_stream(xdc, stream_id) != NULL;
    mtx_unlock(&inst->lock);

    mtx_unlock(&xdc->instance_list_lock);

    // Notify the host that this stream id is available on the debug device.
    xdc_notify_stream_state(xdc, stream_id, true /* online */);

    mtx_lock(&xdc->write_lock);
    xdc_update_instance_write_signal(inst, xdc->writable);
    mtx_unlock(&xdc->write_lock);

    zxlogf(TRACE, "registered stream id %u\n", stream_id);
    return ZX_OK;
}

// Attempts to requeue the request on the IN endpoint.
// If not successful, the request is returned to the free_read_reqs list.
static void xdc_queue_read_locked(xdc_t* xdc, usb_request_t* req) __TA_REQUIRES(xdc->read_lock) {
    zx_status_t status = xdc_queue_transfer(xdc, req, true /** in **/, false /* is_ctrl_msg */);
    if (status != ZX_OK) {
        zxlogf(ERROR, "xdc_read failed to re-queue request %d\n", status);
        list_add_tail(&xdc->free_read_reqs, &req->node);
    }
}

static void xdc_update_instance_read_signal_locked(xdc_instance_t* inst)
                                                   __TA_REQUIRES(inst->lock) {
    if (list_length(&inst->completed_reads) > 0) {
        device_state_set(inst->zxdev, DEV_STATE_READABLE);
    } else {
        device_state_clr(inst->zxdev, DEV_STATE_READABLE);
    }
}

static zx_status_t xdc_read_instance(void* ctx, void* buf, size_t count,
                                     zx_off_t off, size_t* actual) {
    xdc_instance_t* inst = ctx;

    mtx_lock(&inst->lock);

    if (inst->dead) {
        mtx_unlock(&inst->lock);
        return ZX_ERR_PEER_CLOSED;
    }

    if (!inst->has_stream_id) {
        zxlogf(ERROR, "read failed, instance %p did not have a valid stream id\n", inst);
        mtx_unlock(&inst->lock);
        return ZX_ERR_BAD_STATE;
    }

    if (list_is_empty(&inst->completed_reads)) {
        mtx_unlock(&inst->lock);
        return ZX_ERR_SHOULD_WAIT;
    }

    list_node_t done_reqs = LIST_INITIAL_VALUE(done_reqs);

    size_t copied = 0;
    usb_request_t* req;
    // Copy up to the requested amount, or until we have no completed read buffers left.
    while ((copied < count) &&
           (req = list_peek_head_type(&inst->completed_reads, usb_request_t, node)) != NULL) {
        if (inst->cur_req_read_offset == 0) {
            bool is_new_packet;
            void* data;
            zx_status_t status = usb_request_mmap(req, &data);
             if (status != ZX_OK) {
                 zxlogf(ERROR, "usb_request_mmap failed, err: %d\n", status);
                 mtx_unlock(&inst->lock);
                 return ZX_ERR_BAD_STATE;
            }

            status = xdc_update_packet_state(&inst->cur_read_packet,
                                             data, req->response.actual, &is_new_packet);
            if (status != ZX_OK) {
                mtx_unlock(&inst->lock);
                return ZX_ERR_BAD_STATE;
            }
            if (is_new_packet) {
                // Skip over the header, which contains internal metadata like stream id.
                inst->cur_req_read_offset += sizeof(xdc_packet_header_t);
            }
        }
        size_t req_bytes_left = req->response.actual - inst->cur_req_read_offset;
        size_t to_copy = MIN(count - copied, req_bytes_left);
        size_t bytes_copied = usb_request_copyfrom(req, buf + copied,
                                                   to_copy, inst->cur_req_read_offset);

        copied += bytes_copied;
        inst->cur_req_read_offset += bytes_copied;

        // Finished copying all the available bytes from this usb request buffer.
        if (inst->cur_req_read_offset >= req->response.actual) {
            list_remove_head(&inst->completed_reads);
            list_add_tail(&done_reqs, &req->node);

            inst->cur_req_read_offset = 0;
        }
    }

    xdc_update_instance_read_signal_locked(inst);
    mtx_unlock(&inst->lock);

    xdc_t* xdc = inst->parent;
    mtx_lock(&xdc->read_lock);
    while ((req = list_remove_tail_type(&done_reqs, usb_request_t, node)) != NULL) {
        xdc_queue_read_locked(xdc, req);
    }
    mtx_unlock(&xdc->read_lock);

    *actual = copied;
    return ZX_OK;
}

static zx_status_t xdc_ioctl_instance(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual) {
    xdc_instance_t* inst = ctx;

    switch (op) {
    case IOCTL_DEBUG_SET_STREAM:
        if (in_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint32_t stream_id = *((int *)in_buf);
        return xdc_register_stream(inst, stream_id);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t xdc_close_instance(void* ctx, uint32_t flags) {
    xdc_instance_t* inst = ctx;

    list_node_t free_reqs = LIST_INITIAL_VALUE(free_reqs);

    mtx_lock(&inst->lock);
    inst->dead = true;
    list_move(&inst->completed_reads, &free_reqs);
    mtx_unlock(&inst->lock);

    mtx_lock(&inst->parent->instance_list_lock);
    list_delete(&inst->node);
    mtx_unlock(&inst->parent->instance_list_lock);

    xdc_t* xdc = inst->parent;
    // Return any unprocessed requests back to the read queue to be reused.
    mtx_lock(&xdc->read_lock);
    usb_request_t* req;
    while ((req = list_remove_tail_type(&free_reqs, usb_request_t, node)) != NULL) {
        xdc_queue_read_locked(xdc, req);
    }
    mtx_unlock(&xdc->read_lock);

    if (inst->has_stream_id) {
        // Notify the host that this stream id is now unavailable on the debug device.
        xdc_notify_stream_state(xdc, inst->stream_id, false /* online */);
    }

    atomic_fetch_add(&xdc->num_instances, -1);

    return ZX_OK;
}

static void xdc_release_instance(void* ctx) {
    xdc_instance_t* inst = ctx;
    free(inst);
}

zx_protocol_device_t xdc_instance_proto = {
    .version = DEVICE_OPS_VERSION,
    .write = xdc_write_instance,
    .read = xdc_read_instance,
    .ioctl = xdc_ioctl_instance,
    .close = xdc_close_instance,
    .release = xdc_release_instance,
};

static zx_status_t xdc_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    xdc_t* xdc = ctx;

    xdc_instance_t* inst = calloc(1, sizeof(xdc_instance_t));
    if (inst == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "xdc",
        .ctx = inst,
        .ops = &xdc_instance_proto,
        .proto_id = ZX_PROTOCOL_USB_DBC,
        .flags = DEVICE_ADD_INSTANCE,
    };

    zx_status_t status = status = device_add(xdc->zxdev, &args, &inst->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "xdc: error creating instance %d\n", status);
        free(inst);
        return status;
    }

    inst->parent = xdc;
    list_initialize(&inst->completed_reads);

    mtx_lock(&xdc->instance_list_lock);
    list_add_tail(&xdc->instance_list, &inst->node);
    mtx_unlock(&xdc->instance_list_lock);

    *dev_out = inst->zxdev;

    atomic_fetch_add(&xdc->num_instances, 1);
    sync_completion_signal(&xdc->has_instance_completion);
    return ZX_OK;

}

static void xdc_shutdown(xdc_t* xdc) {
    zxlogf(TRACE, "xdc_shutdown\n");

    atomic_store(&xdc->suspended, true);
    // The poll thread will be waiting on this completion if no instances are open.
    sync_completion_signal(&xdc->has_instance_completion);

    int res;
    thrd_join(xdc->start_thread, &res);
    if (res != 0) {
        zxlogf(ERROR, "failed to join with xdc start_thread\n");
    }

    XHCI_WRITE32(&xdc->debug_cap_regs->dcctrl, 0);
    xdc_wait_bits(&xdc->debug_cap_regs->dcctrl, DCCTRL_DCR, 0);

    mtx_lock(&xdc->lock);
    xdc->configured = false;

    for (uint32_t i = 0; i < NUM_EPS; ++i) {
        xdc_endpoint_t* ep = &xdc->eps[i];
        ep->state = XDC_EP_STATE_DEAD;

        usb_request_t* req;
        while ((req = list_remove_tail_type(&ep->pending_reqs, usb_request_t, node)) != NULL) {
            usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0);
        }
        while ((req = list_remove_tail_type(&ep->queued_reqs, usb_request_t, node)) != NULL) {
            usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0);
        }
    }

    mtx_unlock(&xdc->lock);

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

    usb_request_pool_release(&xdc->free_write_reqs);

    usb_request_t* req;
    while ((req = list_remove_tail_type(&xdc->free_read_reqs, usb_request_t, node)) != NULL) {
        usb_request_release(req);
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

    mtx_lock(&xdc->instance_list_lock);
    xdc_instance_t* inst;
    list_for_every_entry(&xdc->instance_list, inst, xdc_instance_t, node) {
        mtx_lock(&inst->lock);

        inst->dead = true;
        // Signal any waiting instances to wake up, so they will close the instance.
        device_state_set(inst->zxdev, DEV_STATE_WRITABLE | DEV_STATE_READABLE);

        mtx_unlock(&inst->lock);
    }
    mtx_unlock(&xdc->instance_list_lock);

    device_remove(xdc->zxdev);
}

static void xdc_release(void* ctx) {
    zxlogf(INFO, "xdc_release\n");
    xdc_t* xdc = ctx;
    xdc_free(xdc);
}

static void xdc_update_write_signal_locked(xdc_t* xdc, bool online)
                                           __TA_REQUIRES(xdc->write_lock) {
    bool was_writable = xdc->writable;
    xdc->writable = online && xdc_has_free_trbs(xdc, false /* in */);
    if (was_writable == xdc->writable) {
        return;
    }

    mtx_lock(&xdc->instance_list_lock);
    xdc_instance_t* inst;
    list_for_every_entry(&xdc->instance_list, inst, xdc_instance_t, node) {
        xdc_update_instance_write_signal(inst, xdc->writable);
    }
    mtx_unlock(&xdc->instance_list_lock);
}

static void xdc_write_complete(usb_request_t* req, void* cookie) {
    xdc_t* xdc = cookie;

    zx_status_t status = req->response.status;
    if (status != ZX_OK) {
        zxlogf(ERROR, "xdc_write_complete got unexpected error: %d\n", req->response.status);
    }

    mtx_lock(&xdc->write_lock);
    usb_request_pool_add(&xdc->free_write_reqs, req);
    xdc_update_write_signal_locked(xdc, status != ZX_ERR_IO_NOT_PRESENT /* online */);
    mtx_unlock(&xdc->write_lock);
}

static zx_status_t xdc_write(xdc_t* xdc, uint32_t stream_id, const void* buf, size_t count,
                             size_t* actual, bool is_ctrl_msg) {
    // TODO(jocelyndang): we should check for requests that are too big to fit on the transfer ring.

    zx_status_t status = ZX_OK;

    mtx_lock(&xdc->write_lock);

    // We should always queue control messages unless there is an unrecoverable error.
    if (!is_ctrl_msg && !xdc->writable) {
        // Need to wait for some requests to complete.
        mtx_unlock(&xdc->write_lock);
        return ZX_ERR_SHOULD_WAIT;
    }

    size_t header_len = sizeof(xdc_packet_header_t);
    xdc_packet_header_t header = {
        .stream_id = stream_id,
        .total_length = header_len + count
    };
    usb_request_t* req = usb_request_pool_get(&xdc->free_write_reqs, header.total_length);
    if (!req) {
        zx_status_t status = usb_request_alloc(&req, xdc->bti_handle,
                                               header.total_length, OUT_EP_ADDR);
        if (status != ZX_OK) {
            goto out;
        }
    }

    usb_request_copyto(req, &header, header_len, 0);
    usb_request_copyto(req, buf, count, header_len /* offset */);
    req->header.length = header.total_length;

    status = xdc_queue_transfer(xdc, req, false /* in */, is_ctrl_msg);
    if (status != ZX_OK) {
        zxlogf(ERROR, "xdc_write failed %d\n", status);
        usb_request_pool_add(&xdc->free_write_reqs, req);
        goto out;
    }

    *actual = count;

out:
    xdc_update_write_signal_locked(xdc, status != ZX_ERR_IO_NOT_PRESENT /* online */);
    mtx_unlock(&xdc->write_lock);
    return status;
}

static void xdc_handle_msg(xdc_t* xdc, xdc_msg_t* msg) {
    switch (msg->opcode) {
    case XDC_NOTIFY_STREAM_STATE: {
        xdc_notify_stream_state_t* state = &msg->notify_stream_state;

        mtx_lock(&xdc->instance_list_lock);

        // Find the saved host stream if it exists.
        xdc_host_stream_t* host_stream = xdc_get_host_stream(xdc, state->stream_id);
        if (state->online == (host_stream != NULL)) {
            zxlogf(ERROR, "cannot set host stream state for id %u as it was already %s\n",
                   state->stream_id, state->online ? "online" : "offline");
            mtx_unlock(&xdc->instance_list_lock);
            return;
        }
        if (state->online) {
            xdc_host_stream_t* host_stream = malloc(sizeof(xdc_host_stream_t));
            if (!host_stream) {
                zxlogf(ERROR, "can't create host stream, out of memory!\n");
                mtx_unlock(&xdc->instance_list_lock);
                return;
            }
            zxlogf(TRACE, "setting host stream id %u as online\n", state->stream_id);
            host_stream->stream_id = state->stream_id;
            list_add_tail(&xdc->host_streams, &host_stream->node);
        } else {
            zxlogf(TRACE, "setting host stream id %u as offline\n", state->stream_id);
            list_delete(&host_stream->node);
        }

        // Check if any instance is registered to this stream id and update its connected status.
        xdc_instance_t* test;
        xdc_instance_t* match = NULL;
        list_for_every_entry(&xdc->instance_list, test, xdc_instance_t, node) {
            mtx_lock(&test->lock);
            if (test->has_stream_id && test->stream_id == state->stream_id) {
                zxlogf(TRACE, "stream id %u is now %s to the host\n",
                       state->stream_id, state->online ? "connected" : "disconnected");
                test->connected = state->online;
                match = test;
                mtx_unlock(&test->lock);
                break;
            }
            mtx_unlock(&test->lock);
        }
        mtx_unlock(&xdc->instance_list_lock);

        if (match) {
            // Notify the instance whether they can now write.
            mtx_lock(&xdc->write_lock);
            xdc_update_instance_write_signal(match, xdc->writable);
            mtx_unlock(&xdc->write_lock);
        }
        return;
    }
    default:
        zxlogf(ERROR, "unrecognized command: %d\n", msg->opcode);
    }
}

static void xdc_read_complete(usb_request_t* req, void* cookie) {
    xdc_t* xdc = cookie;

    mtx_lock(&xdc->read_lock);

    if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
        list_add_tail(&xdc->free_read_reqs, &req->node);
        goto out;
    }

    if (req->response.status != ZX_OK) {
        zxlogf(ERROR, "xdc_read_complete: req completion status = %d", req->response.status);
        xdc_queue_read_locked(xdc, req);
        goto out;
    }

    void* data;
    zx_status_t status = usb_request_mmap(req, &data);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_request_mmap failed, err: %d\n", status);
        xdc_queue_read_locked(xdc, req);
        goto out;
    }
    bool new_header;
    status = xdc_update_packet_state(&xdc->cur_read_packet, data, req->response.actual,
                                     &new_header);
    if (status != ZX_OK) {
        xdc_queue_read_locked(xdc, req);
        goto out;
    }

    if (new_header && xdc->cur_read_packet.header.stream_id == XDC_MSG_STREAM) {
        size_t offset = sizeof(xdc_packet_header_t);
        if (req->response.actual - offset < sizeof(xdc_msg_t)) {
            zxlogf(ERROR, "malformed xdc ctrl msg, len was %lu want %lu\n",
                   req->response.actual - offset, sizeof(xdc_msg_t));
            xdc_queue_read_locked(xdc, req);
            goto out;
        }
        xdc_msg_t msg;
        usb_request_copyfrom(req, &msg, sizeof(xdc_msg_t), offset);

        // We should process the control message outside of the lock, so requeue the request now.
        xdc_queue_read_locked(xdc, req);
        mtx_unlock(&xdc->read_lock);

        xdc_handle_msg(xdc, &msg);
        return;
    }

    // Find the instance that is registered for the stream id of the message.
    mtx_lock(&xdc->instance_list_lock);

    bool found = false;
    xdc_instance_t* inst;
    list_for_every_entry(&xdc->instance_list, inst, xdc_instance_t, node) {
        mtx_lock(&inst->lock);
        if (inst->has_stream_id && !inst->dead &&
            (inst->stream_id == xdc->cur_read_packet.header.stream_id)) {
            list_add_tail(&inst->completed_reads, &req->node);
            xdc_update_instance_read_signal_locked(inst);
            found = true;
            mtx_unlock(&inst->lock);
            break;
        }
        mtx_unlock(&inst->lock);
    }

    mtx_unlock(&xdc->instance_list_lock);

    if (!found) {
        zxlogf(ERROR, "read packet for stream id %u, but it is not currently registered\n",
               xdc->cur_read_packet.header.stream_id);
        xdc_queue_read_locked(xdc, req);
    }

out:
    mtx_unlock(&xdc->read_lock);
}

static zx_protocol_device_t xdc_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = xdc_open,
    .suspend = xdc_suspend,
    .unbind = xdc_unbind,
    .release = xdc_release,
};

static void xdc_handle_port_status_change(xdc_t* xdc, xdc_poll_state_t* poll_state) {
    uint32_t dcportsc = XHCI_READ32(&xdc->debug_cap_regs->dcportsc);

    if (dcportsc & DCPORTSC_CSC) {
        poll_state->connected = dcportsc & DCPORTSC_CCS;
        if (poll_state->connected) {
            poll_state->last_conn = zx_clock_get_monotonic();
        }
        zxlogf(TRACE, "Port: Connect Status Change, connected: %d\n", poll_state->connected != 0);
    }
    if (dcportsc & DCPORTSC_PRC) {
        zxlogf(TRACE, "Port: Port Reset complete\n");
    }
    if (dcportsc & DCPORTSC_PLC) {
        zxlogf(TRACE, "Port: Port Link Status Change\n");
    }
    if (dcportsc & DCPORTSC_CEC) {
        zxlogf(TRACE, "Port: Port Config Error detected\n");
    }

    // Ack change events.
    XHCI_WRITE32(&xdc->debug_cap_regs->dcportsc, dcportsc);
}

static void xdc_handle_events(xdc_t* xdc, xdc_poll_state_t* poll_state) {
    xhci_event_ring_t* er = &xdc->event_ring;

    // process all TRBs with cycle bit matching our CCS
    while ((XHCI_READ32(&er->current->control) & TRB_C) == er->ccs) {
        uint32_t type = trb_get_type(er->current);
        switch (type) {
        case TRB_EVENT_PORT_STATUS_CHANGE:
            xdc_handle_port_status_change(xdc, poll_state);
            break;
        case TRB_EVENT_TRANSFER:
            mtx_lock(&xdc->lock);
            xdc_handle_transfer_event_locked(xdc, poll_state, er->current);
            mtx_unlock(&xdc->lock);
            break;
        default:
            zxlogf(ERROR, "xdc_handle_events: unhandled event type %d\n", type);
            break;
        }

        er->current++;
        if (er->current == er->end) {
            er->current = er->start;
            er->ccs ^= TRB_C;
        }
    }
    xdc_update_erdp(xdc);
}

// Returns whether we just entered the Configured state.
bool xdc_update_state(xdc_t* xdc, xdc_poll_state_t* poll_state) {
    uint32_t dcst = XHCI_GET_BITS32(&xdc->debug_cap_regs->dcst, DCST_ER_NOT_EMPTY_START,
                                    DCST_ER_NOT_EMPTY_BITS);
    if (dcst) {
        xdc_handle_events(xdc, poll_state);
    }

    uint32_t dcctrl = XHCI_READ32(&xdc->debug_cap_regs->dcctrl);

    if (dcctrl & DCCTRL_DRC) {
        zxlogf(TRACE, "xdc configured exit\n");
        // Need to clear the bit to re-enable the DCDB.
        // TODO(jocelyndang): check if we need to update the transfer ring as per 7.6.4.4.
        XHCI_WRITE32(&xdc->debug_cap_regs->dcctrl, dcctrl);
        poll_state->configured = false;

        mtx_lock(&xdc->lock);
        xdc->configured = false;
        mtx_unlock(&xdc->lock);
    }

    bool entered_configured = false;
    // Just entered the Configured state.
    if (!poll_state->configured && (dcctrl & DCCTRL_DCR)) {
        uint32_t port = XHCI_GET_BITS32(&xdc->debug_cap_regs->dcst, DCST_PORT_NUM_START,
                                        DCST_PORT_NUM_BITS);
        if (port == 0) {
            zxlogf(ERROR, "xdc could not get port number\n");
        } else {
            entered_configured = true;
            poll_state->configured = true;

            mtx_lock(&xdc->lock);

            xdc->configured = true;
            zxlogf(INFO, "xdc configured on port: %u\n", port);

            // We just entered configured mode, so endpoints are ready. Queue any waiting messages.
            for (int i = 0; i < NUM_EPS; i++) {
                xdc_process_transactions_locked(xdc, &xdc->eps[i]);
            }

            mtx_unlock(&xdc->lock);
        }
    }

    // If it takes too long to enter the configured state, we should toggle the
    // DCE bit to retry the Debug Device enumeration process. See last paragraph of
    // 7.6.4.1 of XHCI spec.
    if (poll_state->connected && !poll_state->configured) {
        zx_duration_t waited_ns = zx_clock_get_monotonic() - poll_state->last_conn;

        if (waited_ns > TRANSITION_CONFIGURED_THRESHOLD) {
            zxlogf(ERROR, "xdc failed to enter configured state, toggling DCE\n");
            XHCI_WRITE32(&xdc->debug_cap_regs->dcctrl, 0);
            XHCI_WRITE32(&xdc->debug_cap_regs->dcctrl, DCCTRL_LSE | DCCTRL_DCE);

            // We won't get the disconnect event from disabling DCE, so update it now.
            poll_state->connected = false;
        }
    }
    return entered_configured;
}

void xdc_endpoint_set_halt_locked(xdc_t* xdc, xdc_poll_state_t* poll_state, xdc_endpoint_t* ep)
                                  __TA_REQUIRES(xdc->lock) {
    bool* halt_state = ep->direction == USB_DIR_OUT ? &poll_state->halt_out : &poll_state->halt_in;
    *halt_state = true;

    switch (ep->state) {
    case XDC_EP_STATE_DEAD:
        return;
    case XDC_EP_STATE_RUNNING:
        zxlogf(TRACE, "%s ep transitioned from running to halted\n", ep->name);
        ep->state = XDC_EP_STATE_HALTED;
        return;
    case XDC_EP_STATE_STOPPED:
        // This shouldn't happen as we don't schedule new TRBs when stopped.
        zxlogf(ERROR, "%s ep transitioned from stopped to halted\n", ep->name);
        ep->state = XDC_EP_STATE_HALTED;
        return;
    case XDC_EP_STATE_HALTED:
        return;  // No change in state.
    default:
        zxlogf(ERROR, "unknown ep state: %d\n", ep->state);
        return;
    }
}

static void xdc_endpoint_clear_halt_locked(xdc_t* xdc, xdc_poll_state_t* poll_state,
                                           xdc_endpoint_t* ep) __TA_REQUIRES(xdc->lock) {
    bool* halt_state = ep->direction == USB_DIR_OUT ? &poll_state->halt_out : &poll_state->halt_in;
    *halt_state = false;

    switch (ep->state) {
    case XDC_EP_STATE_DEAD:
    case XDC_EP_STATE_RUNNING:
        return;  // No change in state.
    case XDC_EP_STATE_STOPPED:
        break;  // Already cleared the halt.
    case XDC_EP_STATE_HALTED:
        // The DbC has received the ClearFeature(ENDPOINT_HALT) request from the host.
        zxlogf(TRACE, "%s ep transitioned from halted to stopped\n", ep->name);
        ep->state = XDC_EP_STATE_STOPPED;
        break;
    default:
        zxlogf(ERROR, "unknown ep state: %d\n", ep->state);
        return;
    }

    // If we get here, we are now in the STOPPED state and the halt has been cleared.
    // We should have processed the error events on the event ring once the halt flag was set,
    // but double-check this is the case.
    if (ep->got_err_event) {
        zx_status_t status = xdc_restart_transfer_ring_locked(xdc, ep);
        if (status != ZX_OK) {
            // This should never fail. If it does, disable the debug capability.
            // TODO(jocelyndang): the polling thread should re-initialize everything
            // if DCE is cleared.
            zxlogf(ERROR, "xdc_restart_transfer_ring got err %d, clearing DCE\n", status);
            XHCI_WRITE32(&xdc->debug_cap_regs->dcctrl, 0);
        }
        ep->got_err_event = false;
    }
}

void xdc_update_endpoint_state(xdc_t* xdc, xdc_poll_state_t* poll_state, xdc_endpoint_t* ep) {
    uint32_t dcctrl = XHCI_READ32(&xdc->debug_cap_regs->dcctrl);
    if (!(dcctrl & DCCTRL_DCR)) {
        // Halt bits are irrelevant when the debug capability isn't in Run Mode.
        return;
    }
    bool halt_state = ep->direction == USB_DIR_OUT ? poll_state->halt_out : poll_state->halt_in;

    uint32_t bit = ep->direction == USB_DIR_OUT ? DCCTRL_HOT : DCCTRL_HIT;
    if (halt_state == !!(dcctrl & bit)) {
        // Nothing has changed.
        return;
    }

    mtx_lock(&xdc->lock);
    if (dcctrl & bit) {
        xdc_endpoint_set_halt_locked(xdc, poll_state, ep);
    } else {
        xdc_endpoint_clear_halt_locked(xdc, poll_state, ep);
    }
    mtx_unlock(&xdc->lock);
}

zx_status_t xdc_poll(xdc_t* xdc) {
    xdc_poll_state_t poll_state;
    list_initialize(&poll_state.completed_reqs);

    for (;;) {
        zxlogf(TRACE, "xdc_poll: waiting for a new instance\n");
        // Wait for at least one active instance before polling.
        sync_completion_wait(&xdc->has_instance_completion, ZX_TIME_INFINITE);
        zxlogf(TRACE, "xdc_poll: instance completion signaled, about to enter poll loop\n");
        sync_completion_reset(&xdc->has_instance_completion);

        for (;;) {
            if (atomic_load(&xdc->suspended)) {
                zxlogf(INFO, "xdc_poll: suspending xdc, shutting down poll thread\n");
                return ZX_OK;
            }
            if (atomic_load(&xdc->num_instances) == 0) {
                // If all pending writes have completed, exit the poll loop.
                mtx_lock(&xdc->lock);
                if (list_is_empty(&xdc->eps[OUT_EP_IDX].pending_reqs)) {
                    zxlogf(TRACE, "xdc_poll: no active instances, exiting inner poll loop\n");
                    mtx_unlock(&xdc->lock);
                    // Wait for a new instance to be active.
                    break;
                }
                mtx_unlock(&xdc->lock);
            }
            bool entered_configured = xdc_update_state(xdc, &poll_state);

            // Check if any EP has halted or recovered.
            for (int i = 0; i < NUM_EPS; i++) {
                xdc_endpoint_t* ep = &xdc->eps[i];
                xdc_update_endpoint_state(xdc, &poll_state, ep);
            }

            // If we just entered the configured state, we should schedule the read requests.
            if (entered_configured) {
                mtx_lock(&xdc->read_lock);
                usb_request_t* req;
                while ((req = list_remove_tail_type(&xdc->free_read_reqs,
                                                    usb_request_t, node)) != NULL) {
                    xdc_queue_read_locked(xdc, req);
                }
                mtx_unlock(&xdc->read_lock);

                mtx_lock(&xdc->write_lock);
                xdc_update_write_signal_locked(xdc, true /* online */);
                mtx_unlock(&xdc->write_lock);
            }

            // Call complete callbacks out of the lock.
            // TODO(jocelyndang): might want a separate thread for this.
            usb_request_t* req;
            while ((req = list_remove_head_type(&poll_state.completed_reqs,
                                                usb_request_t, node)) != NULL) {
                usb_request_complete(req, req->response.status, req->response.actual);
            }
        }
    }
    return ZX_OK;
}

static int xdc_start_thread(void* arg) {
    xdc_t* xdc = arg;

    zxlogf(TRACE, "about to enable XHCI DBC\n");
    XHCI_WRITE32(&xdc->debug_cap_regs->dcctrl, DCCTRL_LSE | DCCTRL_DCE);

    return xdc_poll(xdc);
}

// This should only be called once in xdc_bind.
static zx_status_t xdc_init_internal(xdc_t* xdc) {
    mtx_init(&xdc->lock, mtx_plain);

    list_initialize(&xdc->instance_list);
    mtx_init(&xdc->instance_list_lock, mtx_plain);

    atomic_init(&xdc->suspended, false);

    list_initialize(&xdc->host_streams);

    sync_completion_reset(&xdc->has_instance_completion);
    atomic_init(&xdc->num_instances, 0);

    usb_request_pool_init(&xdc->free_write_reqs);
    mtx_init(&xdc->write_lock, mtx_plain);

    list_initialize(&xdc->free_read_reqs);
    mtx_init(&xdc->read_lock, mtx_plain);

    // Allocate the usb requests for write / read.
    for (int i = 0; i < MAX_REQS; i++) {
        usb_request_t* req;
        zx_status_t status = usb_request_alloc(&req, xdc->bti_handle, MAX_REQ_SIZE, OUT_EP_ADDR);
        if (status != ZX_OK) {
            zxlogf(ERROR, "xdc failed to alloc write usb requests, err: %d\n", status);
            return status;
        }
        req->complete_cb = xdc_write_complete;
        req->cookie = xdc;
        usb_request_pool_add(&xdc->free_write_reqs, req);
    }
    for (int i = 0; i < MAX_REQS; i++) {
        usb_request_t* req;
        zx_status_t status = usb_request_alloc(&req, xdc->bti_handle, MAX_REQ_SIZE, IN_EP_ADDR);
        if (status != ZX_OK) {
            zxlogf(ERROR, "xdc failed to alloc read usb requests, err: %d\n", status);
            return status;
        }
        req->complete_cb = xdc_read_complete;
        req->cookie = xdc;
        list_add_head(&xdc->free_read_reqs, &req->node);
    }
    return ZX_OK;
}

zx_status_t xdc_bind(zx_device_t* parent, zx_handle_t bti_handle, void* mmio) {
    xdc_t* xdc = calloc(1, sizeof(xdc_t));
    if (!xdc) {
        return ZX_ERR_NO_MEMORY;
    }
    xdc->bti_handle = bti_handle;
    xdc->mmio = mmio;

    zx_status_t status = xdc_init_internal(xdc);
    if (status != ZX_OK) {
        goto error_return;
    }
    status = xdc_get_debug_cap(xdc);
    if (status != ZX_OK) {
        zxlogf(ERROR, "xdc_get_debug_cap, err: %d\n", status);
        goto error_return;
    }
    status = xdc_init_debug_cap(xdc);
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
