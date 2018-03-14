// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-function.h>
#include <ddk/usb-request.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/device/usb-device.h>
#include <zircon/hw/usb-mass-storage.h>

#define BLOCK_SIZE      512
#define STORAGE_SIZE    (10 * 1024 * 1024)
#define BLOCK_COUNT     (STORAGE_SIZE / BLOCK_SIZE)
#define DATA_REQ_SIZE   16384
#define BULK_MAX_PACKET 512

typedef enum {
    DATA_STATE_NONE,
    DATA_STATE_READ,
    DATA_STATE_WRITE,
} ums_data_state_t;

 static struct {
    usb_interface_descriptor_t intf;
    usb_endpoint_descriptor_t out_ep;
    usb_endpoint_descriptor_t in_ep;
} descriptors = {
    .intf = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
//      .bInterfaceNumber set later
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_MSC,
        .bInterfaceSubClass = USB_SUBCLASS_MSC_SCSI,
        .bInterfaceProtocol = USB_PROTOCOL_MSC_BULK_ONLY,
        .iInterface = 0,
    },
    .out_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
//      .bEndpointAddress set later
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(BULK_MAX_PACKET),
        .bInterval = 0,
    },
    .in_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
//      .bEndpointAddress set later
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(BULK_MAX_PACKET),
        .bInterval = 0,
    },
};

typedef struct {
    zx_device_t* zxdev;
    usb_function_protocol_t function;
    usb_request_t* cbw_req;
    usb_request_t* data_req;
    usb_request_t* csw_req;

    // vmo for backing storage
    zx_handle_t storage_handle;
    void* storage;

    // command we are currently handling
    ums_cbw_t current_cbw;
    // data transferred for the current command
    uint32_t data_length;

    // state for data transfers
    ums_data_state_t    data_state;
    // state for reads and writes
    zx_off_t data_offset;
    size_t data_remaining;

    uint8_t bulk_out_addr;
    uint8_t bulk_in_addr;
} usb_ums_t;

static void ums_function_queue_data(usb_ums_t* ums, usb_request_t* req) {
    ums->data_length += req->header.length;
    req->header.ep_address = ums->current_cbw.bmCBWFlags & USB_DIR_IN ?
        ums->bulk_in_addr : ums->bulk_out_addr;
    usb_function_queue(&ums->function, req);
}

static void ums_queue_csw(usb_ums_t* ums, uint8_t status) {
    // first queue next cbw so it is ready to go
    usb_function_queue(&ums->function, ums->cbw_req);

    usb_request_t* req = ums->csw_req;
    ums_csw_t* csw;
    usb_request_mmap(req, (void **)&csw);

    csw->dCSWSignature = htole32(CSW_SIGNATURE);
    csw->dCSWTag = ums->current_cbw.dCBWTag;
    csw->dCSWDataResidue = htole32(le32toh(ums->current_cbw.dCBWDataTransferLength)
                                   - ums->data_length);
    csw->bmCSWStatus = status;

    req->header.length = sizeof(ums_csw_t);
    usb_function_queue(&ums->function, ums->csw_req);
}

static void ums_continue_transfer(usb_ums_t* ums) {
    usb_request_t* req = ums->data_req;

    size_t length = ums->data_remaining;
    if (length > DATA_REQ_SIZE) {
        length = DATA_REQ_SIZE;
    }
    req->header.length = length;

    if (ums->data_state == DATA_STATE_READ) {
        usb_request_copyto(req, ums->storage + ums->data_offset, length, 0);
        ums_function_queue_data(ums, req);
    } else if (ums->data_state == DATA_STATE_WRITE) {
        ums_function_queue_data(ums, req);
    } else {
        zxlogf(ERROR, "ums_continue_transfer: bad data state %d\n", ums->data_state);
    }
}

static void ums_start_transfer(usb_ums_t* ums, ums_data_state_t state, uint64_t lba,
                               uint32_t blocks) {
    zx_off_t offset = lba * BLOCK_SIZE;
    size_t length = blocks * BLOCK_SIZE;

    if (offset + length > STORAGE_SIZE) {
        zxlogf(ERROR, "ums_start_transfer: transfer out of range state: %d, lba: %zu blocks: %u\n",
               state, lba, blocks);
        // TODO(voydanoff) report error to host
        return;
    }

    ums->data_state = state;
    ums->data_offset = offset;
    ums->data_remaining = length;

    ums_continue_transfer(ums);
}

static void ums_handle_inquiry(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_inquiry\n");

    usb_request_t* req = ums->data_req;
    uint8_t* buffer;
    usb_request_mmap(req, (void **)&buffer);
    memset(buffer, 0, UMS_INQUIRY_TRANSFER_LENGTH);
    req->header.length = UMS_INQUIRY_TRANSFER_LENGTH;

    // fill in inquiry result
    buffer[0] = 0;      // Peripheral Device Type: Direct access block device
    buffer[1] = 0x80;    // Removable
    buffer[2] = 6;       // Version SPC-4
    buffer[3] = 0x12;    // Response Data Format
    memcpy(buffer + 8, "Google  ", 8);
    memcpy(buffer + 16, "Zircon UMS      ", 16);
    memcpy(buffer + 32, "1.00", 4);

    ums_function_queue_data(ums, req);
    ums_queue_csw(ums, CSW_SUCCESS);
}

static void ums_handle_test_unit_ready(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_test_unit_ready\n");

    // no data phase here. Just return status OK
    ums_queue_csw(ums, CSW_SUCCESS);
}

static void ums_handle_request_sense(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_request_sense\n");

    usb_request_t* req = ums->data_req;
    uint8_t* buffer;
    usb_request_mmap(req, (void **)&buffer);
    memset(buffer, 0, UMS_REQUEST_SENSE_TRANSFER_LENGTH);
    req->header.length = UMS_REQUEST_SENSE_TRANSFER_LENGTH;

    // TODO(voydanoff) This is a hack. Figure out correct values to return here.
    buffer[0] = 0x70;   // Response Code
    buffer[2] = 5;      // Illegal Request
    buffer[7] = 10;     // Additional Sense Length
    buffer[12] = 0x20;  // Additional Sense Code

    ums_function_queue_data(ums, req);
    ums_queue_csw(ums, CSW_SUCCESS);
}

static void ums_handle_read_capacity10(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_read_capacity10\n");

    usb_request_t* req = ums->data_req;
    scsi_read_capacity_10_t* data;
    usb_request_mmap(req, (void **)&data);

    uint64_t lba = BLOCK_COUNT - 1;
    if (lba > UINT32_MAX) {
        data->lba = htobe32(UINT32_MAX);
    } else {
        data->lba = htobe32(lba);
    }
    data->block_length = htobe32(BLOCK_SIZE);

    req->header.length = sizeof(*data);
    ums_function_queue_data(ums, req);
    ums_queue_csw(ums, CSW_SUCCESS);
}

static void ums_handle_read_capacity16(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_read_capacity16\n");

    usb_request_t* req = ums->data_req;
    scsi_read_capacity_16_t* data;
    usb_request_mmap(req, (void **)&data);
    memset(data, 0, sizeof(*data));

    data->lba = htobe64(BLOCK_COUNT - 1);
    data->block_length = htobe32(BLOCK_SIZE);

    req->header.length = sizeof(*data);
    ums_function_queue_data(ums, req);
    ums_queue_csw(ums, CSW_SUCCESS);
}

static void ums_handle_mode_sense6(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_mode_sense6\n");

    usb_request_t* req = ums->data_req;
    scsi_mode_sense_6_data_t* data;
    usb_request_mmap(req, (void **)&data);
    memset(data, 0, sizeof(*data));

    // TODO(voydanoff) fill in data here

    req->header.length = sizeof(*data);
    ums_function_queue_data(ums, req);
    ums_queue_csw(ums, CSW_SUCCESS);
}

static void ums_handle_read10(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_read10\n");

    scsi_command10_t* command = (scsi_command10_t *)cbw->CBWCB;
    uint32_t lba = be32toh(command->lba);
    uint32_t blocks = ((uint32_t)command->length_hi << 8) | (uint32_t)command->length_lo;
    ums_start_transfer(ums, DATA_STATE_READ, lba, blocks);
}

static void ums_handle_read12(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_read12\n");

    scsi_command12_t* command = (scsi_command12_t *)cbw->CBWCB;
    uint64_t lba = be32toh(command->lba);
    uint32_t blocks = be32toh(command->length);
    ums_start_transfer(ums, DATA_STATE_READ, lba, blocks);
}

static void ums_handle_read16(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_read16\n");

    scsi_command16_t* command = (scsi_command16_t *)cbw->CBWCB;
    uint32_t lba = be64toh(command->lba);
    uint32_t blocks = be32toh(command->length);
    ums_start_transfer(ums, DATA_STATE_READ, lba, blocks);
}

static void ums_handle_write10(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_write10\n");

    scsi_command10_t* command = (scsi_command10_t *)cbw->CBWCB;
    uint32_t lba = be32toh(command->lba);
    uint32_t blocks = ((uint32_t)command->length_hi << 8) | (uint32_t)command->length_lo;
    ums_start_transfer(ums, DATA_STATE_WRITE, lba, blocks);
}

static void ums_handle_write12(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_write12\n");

    scsi_command12_t* command = (scsi_command12_t *)cbw->CBWCB;
    uint64_t lba = be32toh(command->lba);
    uint32_t blocks = be32toh(command->length);
    ums_start_transfer(ums, DATA_STATE_WRITE, lba, blocks);
}

static void ums_handle_write16(usb_ums_t* ums, ums_cbw_t* cbw) {
    zxlogf(TRACE, "ums_handle_write16\n");

    scsi_command16_t* command = (scsi_command16_t *)cbw->CBWCB;
    uint64_t lba = be64toh(command->lba);
    uint32_t blocks = be32toh(command->length);
    ums_start_transfer(ums, DATA_STATE_WRITE, lba, blocks);
}

static void ums_handle_cbw(usb_ums_t* ums, ums_cbw_t* cbw) {
    if (le32toh(cbw->dCBWSignature) != CBW_SIGNATURE) {
        zxlogf(ERROR, "ums_handle_cbw: bad dCBWSignature 0x%x\n", le32toh(cbw->dCBWSignature));
        return;
    }

    // reset data length for computing residue
    ums->data_length = 0;

    // all SCSI commands have opcode in the same place, so using scsi_command6_t works here.
    scsi_command6_t* command = (scsi_command6_t *)cbw->CBWCB;
    switch (command->opcode) {
    case UMS_INQUIRY:
        ums_handle_inquiry(ums, cbw);
        break;
    case UMS_TEST_UNIT_READY:
        ums_handle_test_unit_ready(ums, cbw);
        break;
    case UMS_REQUEST_SENSE:
        ums_handle_request_sense(ums, cbw);
        break;
    case UMS_READ_CAPACITY10:
        ums_handle_read_capacity10(ums, cbw);
        break;
    case UMS_READ_CAPACITY16:
        ums_handle_read_capacity16(ums, cbw);
        break;
    case UMS_MODE_SENSE6:
        ums_handle_mode_sense6(ums, cbw);
        break;
    case UMS_READ10:
        ums_handle_read10(ums, cbw);
        break;
    case UMS_READ12:
        ums_handle_read12(ums, cbw);
        break;
    case UMS_READ16:
        ums_handle_read16(ums, cbw);
        break;
    case UMS_WRITE10:
        ums_handle_write10(ums, cbw);
        break;
    case UMS_WRITE12:
        ums_handle_write12(ums, cbw);
        break;
    case UMS_WRITE16:
        ums_handle_write16(ums, cbw);
        break;
    default:
        zxlogf(TRACE, "ums_handle_cbw: unsupported opcode %d\n", command->opcode);
        if (cbw->dCBWDataTransferLength) {
            // queue zero length packet to satisfy data phase
            usb_request_t* req = ums->data_req;
            req->header.length = 0;
            ums_function_queue_data(ums, req);
        }
        ums_queue_csw(ums, CSW_FAILED);
        break;
    }
}

static void ums_cbw_complete(usb_request_t* req, void* cookie) {
    usb_ums_t* ums = cookie;

    zxlogf(TRACE, "ums_cbw_complete %d %ld\n", req->response.status, req->response.actual);

    if (req->response.status == ZX_OK && req->response.actual == sizeof(ums_cbw_t)) {
        ums_cbw_t* cbw = &ums->current_cbw;
        usb_request_copyfrom(req, cbw, sizeof(*cbw), 0);
        ums_handle_cbw(ums, cbw);
    }
}

static void ums_data_complete(usb_request_t* req, void* cookie) {
    usb_ums_t* ums = cookie;

    zxlogf(TRACE, "ums_data_complete %d %ld\n", req->response.status, req->response.actual);

    if (ums->data_state == DATA_STATE_WRITE) {
        usb_request_copyfrom(req, ums->storage + ums->data_offset, req->response.actual, 0);
    } else if (ums->data_state != DATA_STATE_READ) {
        return;
    }

    ums->data_offset += req->response.actual;
    if (ums->data_remaining > req->response.actual) {
        ums->data_remaining -= req->response.actual;
    } else {
        ums->data_remaining = 0;
    }

    if (ums->data_remaining > 0) {
        ums_continue_transfer(ums);
    } else {
        ums->data_state = DATA_STATE_NONE;
        ums_queue_csw(ums, CSW_SUCCESS);
    }
}

static void ums_csw_complete(usb_request_t* req, void* cookie) {
    zxlogf(TRACE, "ums_csw_complete %d %ld\n", req->response.status, req->response.actual);
}

static const usb_descriptor_header_t* ums_get_descriptors(void* ctx, size_t* out_length) {
    *out_length = sizeof(descriptors);
    return (const usb_descriptor_header_t *)&descriptors;
}

static zx_status_t ums_control(void* ctx, const usb_setup_t* setup, void* buffer,
                                         size_t length, size_t* out_actual) {
    if (setup->bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
        setup->bRequest == USB_REQ_GET_MAX_LUN && setup->wValue == 0 && setup->wIndex == 0 &&
        setup->wLength >= sizeof(uint8_t)) {
        *((uint8_t *)buffer) = 0;
        *out_actual = sizeof(uint8_t);
        return ZX_OK;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t ums_set_configured(void* ctx, bool configured, usb_speed_t speed) {
    zxlogf(TRACE, "ums_set_configured %d %d\n", configured, speed);
    usb_ums_t* ums = ctx;
    zx_status_t status;

    // TODO(voydanoff) fullspeed and superspeed support
    if (configured) {
        if ((status = usb_function_config_ep(&ums->function, &descriptors.out_ep, NULL)) != ZX_OK ||
            (status = usb_function_config_ep(&ums->function, &descriptors.in_ep, NULL)) != ZX_OK) {
            zxlogf(ERROR, "ums_set_configured: usb_function_config_ep failed\n");
        }
    } else {
        if ((status = usb_function_disable_ep(&ums->function, ums->bulk_out_addr)) != ZX_OK ||
            (status = usb_function_disable_ep(&ums->function, ums->bulk_in_addr)) != ZX_OK) {
            zxlogf(ERROR, "ums_set_configured: usb_function_disable_ep failed\n");
        }
    }

    if (configured && status == ZX_OK) {
        // queue first read on OUT endpoint
        usb_function_queue(&ums->function, ums->cbw_req);
    }
    return status;
}

static zx_status_t ums_set_interface(void* ctx, unsigned interface, unsigned alt_setting) {
    return ZX_ERR_NOT_SUPPORTED;
}

usb_function_interface_ops_t ums_device_ops = {
    .get_descriptors = ums_get_descriptors,
    .control = ums_control,
    .set_configured = ums_set_configured,
    .set_interface = ums_set_interface,
};

static void usb_ums_unbind(void* ctx) {
    zxlogf(TRACE, "usb_ums_unbind\n");
    usb_ums_t* ums = ctx;
    device_remove(ums->zxdev);
}

static void usb_ums_release(void* ctx) {
    zxlogf(TRACE, "usb_ums_release\n");
    usb_ums_t* ums = ctx;

    if (ums->storage) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)ums->storage, STORAGE_SIZE);
    }
    zx_handle_close(ums->storage_handle);

    if (ums->cbw_req) {
        usb_request_release(ums->cbw_req);
    }
    if (ums->data_req) {
        usb_request_release(ums->data_req);
    }
    if (ums->cbw_req) {
        usb_request_release(ums->csw_req);
    }
    free(ums);
}

static zx_protocol_device_t usb_ums_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_ums_unbind,
    .release = usb_ums_release,
};

zx_status_t usb_ums_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "usb_ums_bind\n");

    usb_ums_t* ums = calloc(1, sizeof(usb_ums_t));
    if (!ums) {
        return ZX_ERR_NO_MEMORY;
    }
    ums->data_state = DATA_STATE_NONE;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB_FUNCTION, &ums->function);
    if (status != ZX_OK) {
        goto fail;
    }

    status = usb_function_alloc_interface(&ums->function, &descriptors.intf.bInterfaceNumber);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_ums_bind: usb_function_alloc_interface failed\n");
        goto fail;
    }
    status = usb_function_alloc_ep(&ums->function, USB_DIR_OUT, &ums->bulk_out_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_ums_bind: usb_function_alloc_ep failed\n");
        goto fail;
    }
    status = usb_function_alloc_ep(&ums->function, USB_DIR_IN, &ums->bulk_in_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_ums_bind: usb_function_alloc_ep failed\n");
        goto fail;
    }

    descriptors.out_ep.bEndpointAddress = ums->bulk_out_addr;
    descriptors.in_ep.bEndpointAddress = ums->bulk_in_addr;

    status = usb_function_req_alloc(&ums->function, &ums->cbw_req, BULK_MAX_PACKET,
                                    ums->bulk_out_addr);
    if (status != ZX_OK) {
        goto fail;
    }
    // Endpoint for data_req depends on current_cbw.bmCBWFlags,
    // and will be set in ums_function_queue_data.
    status = usb_function_req_alloc(&ums->function, &ums->data_req, DATA_REQ_SIZE, 0);
    if (status != ZX_OK) {
        goto fail;
    }
    status = usb_function_req_alloc(&ums->function, &ums->csw_req, BULK_MAX_PACKET,
                                    ums->bulk_in_addr);
    if (status != ZX_OK) {
        goto fail;
    }

    // create and map a VMO
    status = zx_vmo_create(STORAGE_SIZE, 0, &ums->storage_handle);
    if (status != ZX_OK) {
        goto fail;
    }
    status = zx_vmar_map(zx_vmar_root_self(), 0, ums->storage_handle, 0, STORAGE_SIZE,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, (zx_vaddr_t *)&ums->storage);
    if (status != ZX_OK) {
        goto fail;
    }

    ums->csw_req->header.length = sizeof(ums_csw_t);
    ums->cbw_req->complete_cb = ums_cbw_complete;
    ums->data_req->complete_cb = ums_data_complete;
    ums->csw_req->complete_cb = ums_csw_complete;
    ums->cbw_req->cookie = ums;
    ums->data_req->cookie = ums;
    ums->csw_req->cookie = ums;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-ums-function",
        .ctx = ums,
        .ops = &usb_ums_proto,
    };

    status = device_add(parent, &args, &ums->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_device_bind add_device failed %d\n", status);
        goto fail;
    }

    usb_function_interface_t intf = {
        .ops = &ums_device_ops,
        .ctx = ums,
    };
    usb_function_register(&ums->function, &intf);

    return ZX_OK;

fail:
    usb_ums_release(ums);
    return status;
}

static zx_driver_ops_t usb_ums_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_ums_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_ums, usb_ums_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_FUNCTION),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_MSC),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_SUBCLASS_MSC_SCSI),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, USB_PROTOCOL_MSC_BULK_ONLY),
ZIRCON_DRIVER_END(usb_ums)
