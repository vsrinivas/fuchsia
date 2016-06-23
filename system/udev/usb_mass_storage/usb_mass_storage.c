// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb.h>
#include <runtime/mutex.h>
#include <runtime/thread.h>
#include <system/listnode.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <endian.h>


#include "sbc3.h"

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define INTR_REQ_COUNT 4
#define USB_BUF_SIZE 2048
#define INTR_REQ_SIZE 8
#define MSD_COMMAND_BLOCK_WRAPPER_SIZE 31
#define MSD_COMMAND_STATUS_WRAPPER_SIZE 13
#define CBW_SIGNATURE 0x43425355
#define CSW_SIGNATURE 0x55425355
#define CSW_SUCCESS 0X00
#define CSW_FAILURE 0X01
#define CSW_PHASE_ERROR 0X02
#define CSW_INVALID 0X03
#define CSW_TAG_MISMATCH 0X04

typedef struct {
    mx_device_t device;
    mx_device_t* usb_device;
    usb_device_protocol_t* device_protocol;
    mx_driver_t* driver;

    uint8_t status[INTR_REQ_SIZE];
    bool busy;
    uint8_t tag;

    usb_endpoint_t* bulk_in;
    usb_endpoint_t* bulk_out;
    usb_endpoint_t* intr_ep;

    // pool of free USB requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;
    list_node_t free_intr_reqs;
    list_node_t queued_reqs;

    // list of received packets not yet read by upper layer
    list_node_t completed_reads;
    list_node_t completed_csws;

    // the last signals we reported
    mx_signals_t signals;
    mxr_mutex_t mutex;
} usb_mass_storage_t;
#define get_usb_mass_storage(dev) containerof(dev, usb_mass_storage_t, device)

static usb_request_t* get_free_write(usb_mass_storage_t* msd) {
    list_node_t* node = list_remove_head(&msd->free_write_reqs);
    if (!node) {
        return 0;
    }
    // zero out memory so buffer is clean
    usb_request_t* request = containerof(node, usb_request_t, node);
    memset(request->buffer, 0, USB_BUF_SIZE);
    return request;
}

static usb_request_t* get_free_read(usb_mass_storage_t* msd) {
    list_node_t* node = list_remove_head(&msd->free_read_reqs);
    if (!node) {
        return 0;
    }
    usb_request_t* request = containerof(node, usb_request_t, node);
    memset(request->buffer, 0, USB_BUF_SIZE);
    return request;
}

static mx_status_t usb_mass_storage_queue_request(usb_mass_storage_t* msd, usb_request_t* request) {
    // necessary to make this function because MSD cannot deal with out of order requests
    printf("in queue request\n");
    if (!(msd->busy)) {
        printf("not busy case\n");
        msd->busy = true;
        return msd->device_protocol->queue_request(msd->usb_device, request);
    } else {
        printf("busy case\n");
        list_add_tail(&msd->queued_reqs, &request->node);
        return 0;
    }
}

static mx_status_t usb_mass_storage_send_cbw(usb_mass_storage_t* msd, uint32_t tag, uint32_t transfer_length, uint8_t flags,
                                             uint8_t lun, uint8_t command_len, void* command) {
    usb_request_t* request = get_free_write(msd);
    if (!request) {
        return ERR_NOT_ENOUGH_BUFFER;
    }
    // CBWs always have 31 bytes
    request->transfer_length = 31;

    // first three blocks are 4 byte
    uint32_t* ptr_32 = (uint32_t*)request->buffer;
    ptr_32[0] = htole32(CBW_SIGNATURE);
    ptr_32[1] = htole32(tag);
    ptr_32[2] = htole32(transfer_length);

    // get a 1 byte pointer and start at 12 because of uint32's
    uint8_t* ptr_8 = (uint8_t*)request->buffer;
    ptr_8[12] = flags;
    ptr_8[13] = lun;
    ptr_8[14] = command_len;

    // copy command_len bytes from the command passed in into the command_len
    memcpy(&(ptr_8[15]), command, (size_t)command_len);
    return usb_mass_storage_queue_request(msd, request);
}

static mx_status_t usb_mass_storage_recv_csw(usb_mass_storage_t* msd) {
    list_node_t* read_node2 = list_remove_head(&msd->free_read_reqs);
    if (!read_node2) {
        return ERR_NOT_ENOUGH_BUFFER;
    }
    usb_request_t* read_request2 = containerof(read_node2, usb_request_t, node);
    read_request2->transfer_length = 13;
    printf("queued read request\n");
    return usb_mass_storage_queue_request(msd, read_request2);
}

// return true if valid CSW, else false
// maybe called process csw?
static mx_status_t usb_mass_storage_verify_csw(usb_request_t* csw_request, usb_request_t* data_request, uint32_t prevtag) {
    // check signature is "USBS"
    uint32_t* ptr_32 = (uint32_t*)csw_request->buffer;
    if (letoh32(ptr_32[0]) != CSW_SIGNATURE) {
        return CSW_INVALID;
    }
    // check if tag matches the tag of last CBW
    if (letoh32(ptr_32[1]) != prevtag) {
        return CSW_TAG_MISMATCH;
    }
    // check if success is true or not?
    uint8_t* ptr_8 = (uint8_t*)(csw_request->buffer);
    if (ptr_8[12] == 0X01) {
        return CSW_FAILURE;
    } else if (ptr_8[12] == 0x02) {
        return CSW_PHASE_ERROR;
    }
    // if yes then success, and check data residue, modify transfer length so valid data only is read
    data_request->transfer_length -= letoh32(ptr_32[2]);
    // to complete read method so that completed read's transfer length is changed to appropriate
    return CSW_SUCCESS;
}

static mx_status_t usb_mass_storage_reset(usb_mass_storage_t* msd) {
    mx_status_t status = msd->device_protocol->control(msd->usb_device, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0xff, 0x00, 0x00, 0, 0);
    printf("resetting, status is: %d\n", (int)status);
    status = msd->device_protocol->control(msd->usb_device, 0x02, 0x01, 0x00, msd->bulk_in->endpoint, 0, 0);
    printf("halting in, status is: %d\n", (int)status);
    status = msd->device_protocol->control(msd->usb_device, 0x02, 0x01, 0x00, msd->bulk_out->endpoint, 0, 0);
    printf("halting out, status is: %d\n", (int)status);
    return status;
}

static mx_status_t usb_mass_storage_get_max_lun(usb_mass_storage_t* msd, void* data) {
    mx_status_t status = msd->device_protocol->control(msd->usb_device, 0xa1, 0xfe, 0x00, 0x00, data, 1);
    printf("getting max lun, status is: %d\n", (int)status);
    return status;
}

static mx_status_t usb_mass_storage_get_endpoint_status(usb_mass_storage_t* msd, usb_endpoint_t* endpoint, void* data) {
    mx_status_t status = msd->device_protocol->control(msd->usb_device, 0x82, 0x00, 0x00, endpoint->endpoint, data, 2);
    printf("getting max lun, status is: %d\n", (int)status);
    return status;
}

static mx_status_t usb_mass_storage_set_config(usb_mass_storage_t* msd, int config) {
    mx_status_t status = msd->device_protocol->control(msd->usb_device, 0x00, 0x09, config, 0x00, 0, 0);
    printf("setting config, status is: %d\n", (int)status);
    return status;
}

static mx_status_t usb_mass_storage_next_request(usb_mass_storage_t* msd) {
    printf("Trying to dequeue next request\n");
    printf("number of requests in list: %d\n", (int)(list_length(&msd->queued_reqs)));
    list_node_t* node = list_remove_head(&msd->queued_reqs);
    if (!node) {
        printf("no more nodes\n");
        msd->busy = false;
        return 0;
    }
    printf("got node, queuing request\n");
    usb_request_t* request = containerof(node, usb_request_t, node);
    mx_status_t status = msd->device_protocol->queue_request(msd->usb_device, request);
    return status;
}

static void update_signals_locked(usb_mass_storage_t* msd) {
    // TODO (voydanoff) signal error state here
    mx_signals_t new_signals = 0;
    if (!list_is_empty(&msd->completed_reads))
        new_signals |= DEV_STATE_READABLE;
    if (!list_is_empty(&msd->free_write_reqs) /*&& msd->online*/)
        new_signals |= DEV_STATE_WRITABLE;
    if (new_signals != msd->signals) {
        device_state_set_clr(&msd->device, new_signals & ~msd->signals, msd->signals & ~new_signals);
        msd->signals = new_signals;
    }
}

static void usb_mass_storage_read_complete(usb_request_t* request) {
    printf("STARTING READ COMPLETE\n");

    usb_mass_storage_t* msd = (usb_mass_storage_t*)request->client_data;

    mxr_mutex_lock(&msd->mutex);
    if (request->status == NO_ERROR) {
        //is a CSW if 13 bytes
        //TODO: add check for correct signature as first bytes
        if (request->transfer_length == 13) {
            list_add_tail(&msd->completed_csws, &request->node);
        } else {
            list_add_tail(&msd->completed_reads, &request->node);
        }
    } else {
        list_add_head(&msd->queued_reqs, &request->node);
    }
    usb_mass_storage_next_request(msd);
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    printf("ENDING READ COMPLETE\n");
}

static void usb_mass_storage_write_complete(usb_request_t* request) {
    printf("STARTING WRITE COMPLETE\n");
    usb_mass_storage_t* msd = (usb_mass_storage_t*)request->client_data;
    // FIXME what to do with error here?
    mxr_mutex_lock(&msd->mutex);
    list_add_tail(&msd->free_write_reqs, &request->node);
    usb_mass_storage_next_request(msd);
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    printf("ENDING WRITE COMPLETE\n");
}

static void usb_mass_storage_interrupt_complete(usb_request_t* request) {
    printf("INTERRUPT HAPPENING?\n");
    usb_mass_storage_t* msd = (usb_mass_storage_t*)request->client_data;
    mxr_mutex_lock(&msd->mutex);
    update_signals_locked(msd);
    list_add_head(&msd->free_intr_reqs, &request->node);
    mxr_mutex_unlock(&msd->mutex);
}

mx_status_t usb_mass_storage_inquiry(mx_device_t* device, uint8_t lun) {
    usb_mass_storage_t* msd = get_usb_mass_storage(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = 0x00000024;
    uint8_t command_length = 6;
    uint8_t* command = malloc(command_length);
    // set command type
    command[0] = MS_INQUIRY;
    // set allocated length in scsi command
    command[4] = 0x24;
    status = usb_mass_storage_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun, command_length, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }
    free(command);

    // read inquiry response
    usb_request_t* read_request = get_free_read(msd);
    if (!read_request) {
        status = ERR_NOT_ENOUGH_BUFFER;
        goto out;
    }
    read_request->transfer_length = 36;
    status = usb_mass_storage_queue_request(msd, read_request);

    // recieve CSW
    status = usb_mass_storage_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t usb_mass_storage_test_unit_ready(mx_device_t* device, uint8_t lun) {
    usb_mass_storage_t* msd = get_usb_mass_storage(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t transfer_length = 0;
    uint8_t command_length = 0x09;
    uint8_t* command = malloc(command_length);
    // set command type
    command[0] = (char)MS_TEST_UNIT_READY;
    status = usb_mass_storage_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun, command_length, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }
    free(command);

    // recieve CSW
    status = usb_mass_storage_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t usb_mass_storage_request_sense(mx_device_t* device, uint8_t lun) {
    printf("starting request sense\n");
    usb_mass_storage_t* msd = get_usb_mass_storage(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = 0x00000012;
    uint8_t command_length = 6;
    uint8_t* command = malloc(command_length);
    // set command type
    command[0] = MS_REQUEST_SENSE;
    // set allocated length in scsi command
    command[4] = 0x12;
    status = usb_mass_storage_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun, command_length, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }
    free(command);

    // read request sense response
    list_node_t* read_node = list_remove_head(&msd->free_read_reqs);
    if (!read_node) {
        status = ERR_NOT_ENOUGH_BUFFER;
        goto out;
    }
    usb_request_t* read_request = containerof(read_node, usb_request_t, node);
    read_request->transfer_length = 18;
    status = usb_mass_storage_queue_request(msd, read_request);

    // recieve CSW
    status = usb_mass_storage_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t usb_mass_storage_read_format_capacities(mx_device_t* device, uint8_t lun) {
    printf("starting request sense\n");
    usb_mass_storage_t* msd = get_usb_mass_storage(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = 0x000000FC;
    uint8_t command_length = 0x0A;
    uint8_t* command = malloc(command_length);
    // set command type
    command[0] = MS_READ_FORMAT_CAPACITIES;
    // set allocated length in scsi command
    command[8] = 0xFC;
    status = usb_mass_storage_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun, command_length, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }
    free(command);

    // read request sense response
    list_node_t* read_node = list_remove_head(&msd->free_read_reqs);
    if (!read_node) {
        status = ERR_NOT_ENOUGH_BUFFER;
        goto out;
    }
    usb_request_t* read_request = containerof(read_node, usb_request_t, node);
    read_request->transfer_length = 252;
    status = usb_mass_storage_queue_request(msd, read_request);

    // recieve CSW
    status = usb_mass_storage_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t usb_mass_storage_read_capacity10(mx_device_t* device, uint8_t lun) {
    printf("starting request sense\n");
    usb_mass_storage_t* msd = get_usb_mass_storage(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = 0x00000008;
    uint8_t command_length = 0x0A;
    uint8_t* command = malloc(command_length);
    // set command type
    command[0] = MS_READ_CAPACITY10;
    status = usb_mass_storage_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun, command_length, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }
    free(command);

    // read request sense response
    list_node_t* read_node = list_remove_head(&msd->free_read_reqs);
    if (!read_node) {
        status = ERR_NOT_ENOUGH_BUFFER;
        goto out;
    }
    usb_request_t* read_request = containerof(read_node, usb_request_t, node);
    read_request->transfer_length = 8;
    status = usb_mass_storage_queue_request(msd, read_request);

    // recieve CSW
    status = usb_mass_storage_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t usb_mass_storage_read_capacity16(mx_device_t* device, uint8_t lun) {
    printf("starting request sense\n");
    usb_mass_storage_t* msd = get_usb_mass_storage(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = 0x00000020;
    uint8_t command_length = 0x10;
    uint8_t* command = malloc(command_length);
    // set command type
    command[0] = MS_READ_CAPACITY16;
    // service action = 16
    command[1] = 0x10;
    status = usb_mass_storage_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun, command_length, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }
    free(command);

    // read request sense response
    list_node_t* read_node = list_remove_head(&msd->free_read_reqs);
    if (!read_node) {
        status = ERR_NOT_ENOUGH_BUFFER;
        goto out;
    }
    usb_request_t* read_request = containerof(read_node, usb_request_t, node);
    read_request->transfer_length = 32;
    status = usb_mass_storage_queue_request(msd, read_request);

    // recieve CSW
    status = usb_mass_storage_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t usb_mass_storage_recv(mx_device_t* device, void* buffer, size_t length) {
    printf("start of regular recv\n");
    usb_mass_storage_t* msd = get_usb_mass_storage(device);
    mx_status_t status = NO_ERROR;
    printf("before regular recv lock\n");
    mxr_mutex_lock(&msd->mutex);
    printf("after regular recv lock\n");
    // int offset = msd->read_offset;
    printf("Num of read reqs completed before trying to read: %d\n", (int)list_length(&msd->completed_reads));
    printf("Num of read reqs waiting before trying to read: %d\n", (int)list_length(&msd->free_read_reqs));

    list_node_t* node = list_peek_head(&msd->completed_reads);
    if (!node) {
        printf("no node :(\n");
        // is this right error code to use?
        status = ERR_NOT_FOUND;
        goto out;
    }
    usb_request_t* request = containerof(node, usb_request_t, node);
    uint8_t* buf = request->buffer;
    // need to change this, as some of the reads will be csws but
    //TODO: change length1 to not a constant
    size_t length1 = 36;
    memcpy(buffer, buf, length1);
    status = length1;

out:
    // msd->read_offset = offset;

    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

static mx_status_t usb_mass_storage_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t usb_mass_storage_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t usb_mass_storage_release(mx_device_t* device) {
    usb_mass_storage_t* msd = get_usb_mass_storage(device);
    free(msd);

    return NO_ERROR;
}

static ssize_t ums_read(mx_device_t* dev, void* data, size_t len, size_t off, void* cookie) {

    return 0;
}

static ssize_t ums_write(mx_device_t* dev, const void* data, size_t len, size_t off, void* cookie) {
    // usb_mass_storage_t* msd = get_usb_mass_storage(dev);
    usb_mass_storage_inquiry(dev, 0);
    usb_mass_storage_request_sense(dev, 0);
    return 0;
}

static mx_protocol_device_t usb_mass_storage_device_proto = {
    .read = ums_read,
    .write = ums_write,
    .release = usb_mass_storage_release,
};

static int usb_mass_storage_start_thread(void* arg) {
    usb_mass_storage_t* msd = (usb_mass_storage_t*)arg;

    mx_status_t status = device_init(&msd->device, msd->driver, "usb_mass_storage", &usb_mass_storage_device_proto);
    if (status != NO_ERROR) {
        free(msd);
        return status;
    }
    printf("starting start_thread\n");
    mxr_mutex_lock(&msd->mutex);
    printf("post lock\n");
    printf("post command\n");
    mxr_mutex_unlock(&msd->mutex);
    printf("unlocked\n");

    // msd->device.protocol_id = MX_PROTOCOL_USB_MASS_STORAGE;
    // msd->device.protocol_ops = &usb_mass_storage_proto;
    device_add(&msd->device, msd->usb_device);

    return NO_ERROR;
}

static mx_status_t usb_mass_storage_bind(mx_driver_t* driver, mx_device_t* device) {
    printf("starting mass storage probe\n");
    usb_device_protocol_t* protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&protocol)) {
        return ERR_NOT_SUPPORTED;
    }
    usb_device_config_t* device_config;
    mx_status_t status = protocol->get_config(device, &device_config);
    if (status < 0)
        return status;

    usb_configuration_t* config = &device_config->configurations[0];
    usb_interface_t* intf = &config->interfaces[0];
    // find our endpoints
    if (intf->num_endpoints < 2) {
        printf("usb_mass_storage_bind wrong number of endpoints: %d\n", intf->num_endpoints);
        return ERR_NOT_SUPPORTED;
    }
    usb_endpoint_t* bulk_in = NULL;
    usb_endpoint_t* bulk_out = NULL;
    usb_endpoint_t* intr_ep = NULL;
    printf("NUM OF ENDPOINTS: %d\n", intf->num_endpoints);
    for (int i = 0; i < intf->num_endpoints; i++) {
        usb_endpoint_t* endp = &intf->endpoints[i];
        if (endp->direction == USB_ENDPOINT_OUT) {
            if (endp->type == USB_ENDPOINT_BULK) {
                printf("HI IM A BULK OUT: %d\n", i);
                bulk_out = endp;
            }
        } else {
            if (endp->type == USB_ENDPOINT_BULK) {
                bulk_in = endp;
                printf("HI IM A BULK IN: %d\n", i);
            } else if (endp->type == USB_ENDPOINT_INTERRUPT) {
                printf("HI IM A BULK INTERRUPT\n");
                intr_ep = endp;
            }
        }
    }
    if (!bulk_in || !bulk_out) {
        printf("usb_mass_storage_bind could not find endpoints\n");
        return ERR_NOT_SUPPORTED;
    }

    usb_mass_storage_t* msd = calloc(1, sizeof(usb_mass_storage_t));
    if (!msd) {
        printf("Not enough memory for usb_mass_storage_t\n");
        return ERR_NO_MEMORY;
    }

    list_initialize(&msd->free_read_reqs);
    list_initialize(&msd->free_write_reqs);
    list_initialize(&msd->free_intr_reqs);
    list_initialize(&msd->queued_reqs);
    list_initialize(&msd->completed_reads);
    list_initialize(&msd->completed_csws);

    msd->usb_device = device;
    msd->driver = driver;
    msd->device_protocol = protocol;
    msd->bulk_in = bulk_in;
    msd->bulk_out = bulk_out;
    msd->intr_ep = intr_ep;

    for (int i = 0; i < READ_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, bulk_in, USB_BUF_SIZE);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = usb_mass_storage_read_complete;
        req->client_data = msd;
        list_add_head(&msd->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, bulk_out, USB_BUF_SIZE);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = usb_mass_storage_write_complete;
        req->client_data = msd;
        list_add_head(&msd->free_write_reqs, &req->node);
    }

    if (msd->intr_ep) {
        for (int i = 0; i < INTR_REQ_COUNT; i++) {
            usb_request_t* req = protocol->alloc_request(device, intr_ep, INTR_REQ_SIZE);
            if (!req)
                return ERR_NO_MEMORY;
            req->complete_cb = usb_mass_storage_interrupt_complete;
            req->client_data = msd;
            list_add_head(&msd->free_intr_reqs, &req->node);
        }
    }

    usb_mass_storage_set_config(msd, 1);
    // int* lun = (int*)malloc(1);
    char lun = 'a';
    usb_mass_storage_get_max_lun(msd, (void*)&lun);
    printf("Max lun is: %02x\n", (unsigned char)lun);

    mxr_thread_t* thread;
    mxr_thread_create(usb_mass_storage_start_thread, msd, "usb_mass_storage_start_thread", &thread);
    mxr_thread_detach(thread);
    update_signals_locked(msd);
    msd->busy = false;
    msd->tag = 8;

    printf("mass storage bind complete\n");
    return NO_ERROR;
}

static mx_status_t usb_mass_storage_unbind(mx_driver_t* drv, mx_device_t* dev) {
    // TODO - cleanup
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_DEVICE),
    BI_MATCH_IF(EQ, BIND_USB_IFC_CLASS, USB_CLASS_MSC),
};

mx_driver_t _driver_usb_mass_storage BUILTIN_DRIVER = {
    .name = "usb_mass_storage",
    .ops = {
        // .probe = usb_mass_storage_probe,
        .bind = usb_mass_storage_bind,
        .unbind = usb_mass_storage_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
