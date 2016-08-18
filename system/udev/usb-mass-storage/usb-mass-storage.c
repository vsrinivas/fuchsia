// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb.h>
#include <runtime/mutex.h>
#include <runtime/thread.h>
#include <system/listnode.h>
#include <ddk/protocol/block.h>

#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ums-hw.h"
#include "usb-mass-storage.h"

#define READ_REQ_COUNT 3
#define WRITE_REQ_COUNT 3
#define USB_BUF_SIZE 0x8000
#define MSD_COMMAND_BLOCK_WRAPPER_SIZE 31
#define MSD_COMMAND_STATUS_WRAPPER_SIZE 13

// comment the next line if you don't want debug messages
// #define DEBUG 0
#ifdef DEBUG
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

typedef struct {
    mx_device_t device;
    mx_device_t* udev;
    usb_device_protocol_t* usb_p;
    mx_driver_t* driver;

    bool busy;
    uint8_t tag;
    uint64_t total_blocks;
    uint32_t block_size;
    uint8_t capacity_descriptor;
    uint8_t read_flag;

    usb_endpoint_t* bulk_in;
    usb_endpoint_t* bulk_out;

    // pool of free USB requests
    list_node_t free_csw_reqs;
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;
    list_node_t queued_reqs;

    // list of queued io transactions
    list_node_t queued_iotxns;


    // list of received packets not yet read by upper layer
    list_node_t completed_reads;
    list_node_t completed_csws;

    // the last signals we reported
    mx_signals_t signals;
    mxr_mutex_t mutex;
    completion_t read_completion;


} ums_t;
#define get_ums(dev) containerof(dev, ums_t, device)

static inline uint16_t read16be(uint8_t* ptr) {
    return betoh16(*((uint16_t*)ptr));
}

static inline uint32_t read32be(uint8_t* ptr) {
    return betoh32(*((uint32_t*)ptr));
}

static inline uint64_t read64be(uint8_t* ptr) {
    return betoh64(*((uint64_t*)ptr));
}

static inline void write16be(uint8_t* ptr, uint16_t n) {
    *((uint16_t*)ptr) = htobe16(n);
}

static inline void write32be(uint8_t* ptr, uint32_t n) {
    *((uint32_t*)ptr) = htobe32(n);
}

static inline void write64be(uint8_t* ptr, uint64_t n) {
    *((uint64_t*)ptr) = htobe64(n);
}

static mx_status_t ums_reset(ums_t* msd) {
    // for all these control requests, data is null, length is 0 because nothing is passed back
    // value and index not used for first command, though index is supposed to be set to interface number
    // TODO: check interface number, see if index needs to be set
    mx_status_t status = msd->usb_p->control(msd->udev, USB_DIR_OUT | USB_TYPE_CLASS
                                            | USB_RECIP_INTERFACE, USB_REQ_RESET, 0x00, 0x00, NULL, 0);
    DEBUG_PRINT(("UMS:resetting, status is: %d\n", (int)status));
    status = msd->usb_p->control(msd->udev, USB_DIR_OUT | USB_TYPE_CLASS
                                           | USB_RECIP_INTERFACE, USB_REQ_CLEAR_FEATURE, FS_ENDPOINT_HALT,
                                           msd->bulk_in->endpoint, NULL, 0);
    DEBUG_PRINT(("UMS:halting in, status is: %d\n", (int)status));
    status = msd->usb_p->control(msd->udev, USB_DIR_OUT | USB_TYPE_CLASS
                                           | USB_RECIP_INTERFACE, USB_REQ_CLEAR_FEATURE, FS_ENDPOINT_HALT,
                                           msd->bulk_out->endpoint, NULL, 0);
    DEBUG_PRINT(("UMS:halting out, status is: %d\n", (int)status));
    return status;
}

static mx_status_t ums_get_max_lun(ums_t* msd, void* data) {
    mx_status_t status = msd->usb_p->control(msd->udev, USB_DIR_IN | USB_TYPE_CLASS
                                    | USB_RECIP_INTERFACE, USB_REQ_GET_MAX_LUN, 0x00, 0x00, data, 1);
    DEBUG_PRINT(("UMS:getting max lun, status is: %d\n", (int)status));
    return status;
}

static mx_status_t ums_get_endpoint_status(ums_t* msd, usb_endpoint_t* endpoint, void* data) {
    mx_status_t status = msd->usb_p->control(msd->udev, USB_DIR_IN | USB_TYPE_CLASS
                                    | USB_RECIP_INTERFACE, USB_REQ_GET_STATUS, 0x00,
                                    endpoint->endpoint, data, 2);
    DEBUG_PRINT(("UMS:getting endpoint status, status is: %d\n", (int)status));
    return status;
}

static usb_request_t* get_free_write(ums_t* msd) {
    list_node_t* node = list_remove_head(&msd->free_write_reqs);
    if (!node) {
        return 0;
    }
    // zero out memory so buffer is clean
    usb_request_t* request = containerof(node, usb_request_t, node);
    memset(request->buffer, 0, USB_BUF_SIZE);
    return request;
}

static usb_request_t* get_free_read(ums_t* msd) {
    list_node_t* node = list_remove_head(&msd->free_read_reqs);
    if (!node) {
        return 0;
    }
    usb_request_t* request = containerof(node, usb_request_t, node);
    memset(request->buffer, 0, USB_BUF_SIZE);
    return request;
}

static mx_status_t ums_queue_request(ums_t* msd, usb_request_t* request) {
    // necessary to make this function because MSD cannot deal with out of order requests
    DEBUG_PRINT(("UMS:in queue request\n"));
    if (!(msd->busy)) {
        DEBUG_PRINT(("UMS:not busy case\n"));
        msd->busy = true;
        return msd->usb_p->queue_request(msd->udev, request);
    } else {
        DEBUG_PRINT(("UMS:busy case\n"));
        list_add_tail(&msd->queued_reqs, &request->node);
        return 0;
    }
}

static mx_status_t ums_send_cbw(ums_t* msd, uint32_t tag, uint32_t transfer_length, uint8_t flags,
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
    memcpy(ptr_8 + 15, command, (size_t)command_len);
    return ums_queue_request(msd, request);
}

static mx_status_t ums_recv_csw(ums_t* msd) {
    list_node_t* csw_node = list_remove_head(&msd->free_csw_reqs);
    if (!csw_node) {
        DEBUG_PRINT(("UMS:error, no CSW reqs left\n"));
        return ERR_NOT_ENOUGH_BUFFER;
    }
    usb_request_t* csw_request = containerof(csw_node, usb_request_t, node);
    csw_request->transfer_length = MSD_COMMAND_STATUS_WRAPPER_SIZE;
    memset(csw_request->buffer, 0, csw_request->transfer_length);
    DEBUG_PRINT(("UMS:queued csw request\n"));
    return ums_queue_request(msd, csw_request);
}

static mx_status_t ums_queue_read(ums_t* msd, uint16_t transfer_length) {
    // read request sense response
    list_node_t* read_node = list_remove_head(&msd->free_read_reqs);
    if (!read_node) {
        DEBUG_PRINT(("UMS:error, no read reqs left\n"));
        return ERR_NOT_ENOUGH_BUFFER;
    }
    usb_request_t* read_request = containerof(read_node, usb_request_t, node);
    read_request->transfer_length = transfer_length;
    return ums_queue_request(msd, read_request);
}

static mx_status_t ums_queue_write(ums_t* msd, uint16_t transfer_length, iotxn_t* txn) {
    list_node_t* write_node = list_remove_head(&msd->free_write_reqs);
    if (!write_node) {
        DEBUG_PRINT(("UMS:error, no write reqs left\n"));
        return ERR_NOT_ENOUGH_BUFFER;
    }
    usb_request_t* write_request = containerof(write_node, usb_request_t, node);
    write_request->transfer_length = transfer_length;
    txn->ops->copyfrom(txn, write_request->buffer, (size_t)transfer_length, 0);
    return ums_queue_request(msd, write_request);
}

// return true if valid CSW, else false
// maybe called process csw?
static csw_status_t ums_verify_csw(usb_request_t* csw_request, uint32_t prevtag) {
    // check signature is "USBS"
    uint32_t* ptr_32 = (uint32_t*)csw_request->buffer;
    if (letoh32(ptr_32[0]) != CSW_SIGNATURE) {
        DEBUG_PRINT(("UMS:invalid csw sig, expected:%08x got:%08x \n", CSW_SIGNATURE, letoh32(ptr_32[0])));
        return CSW_INVALID;
    }
    // check if tag matches the tag of last CBW
    if (letoh32(ptr_32[1]) != prevtag) {
        DEBUG_PRINT(("UMS:csw tag mismatch, expected:%08x got in csw:%08x \n", prevtag, letoh32(ptr_32[1])));
        return CSW_TAG_MISMATCH;
    }
    // check if success is true or not?
    uint8_t* ptr_8 = (uint8_t*)(csw_request->buffer);
    if (ptr_8[12] == CSW_FAILED) {
        return CSW_FAILED;
    } else if (ptr_8[12] == CSW_PHASE_ERROR) {
        return CSW_PHASE_ERROR;
    }
    return CSW_SUCCESS;
}

static mx_status_t ums_next_request(ums_t* msd) {
    DEBUG_PRINT(("UMS:Trying to dequeue next request\n"));
    DEBUG_PRINT(("UMS:number of requests in list: %d\n", (int)(list_length(&msd->queued_reqs))));
    list_node_t* node = list_remove_head(&msd->queued_reqs);
    if (!node) {
        DEBUG_PRINT(("UMS:no more nodes\n"));
        msd->busy = false;
        return 0;
    }
    DEBUG_PRINT(("UMS:got node, queuing request\n"));
    usb_request_t* request = containerof(node, usb_request_t, node);
    mx_status_t status = msd->usb_p->queue_request(msd->udev, request);
    return status;
}

static void ums_read_complete(usb_request_t* request) {
    DEBUG_PRINT(("UMS:STARTING READ COMPLETE\n"));

    ums_t* msd = (ums_t*)request->client_data;

    mxr_mutex_lock(&msd->mutex);
    if (request->status == NO_ERROR) {
        list_add_tail(&msd->completed_reads, &request->node);
    } else {
        list_add_head(&msd->queued_reqs, &request->node);
    }
    ums_next_request(msd);
    mxr_mutex_unlock(&msd->mutex);
    DEBUG_PRINT(("UMS:ENDING READ COMPLETE\n"));
}

static void ums_csw_complete(usb_request_t* csw_request) {
    DEBUG_PRINT(("UMS:STARTING CSW COMPLETE\n"));

    ums_t* msd = (ums_t*)csw_request->client_data;

    mxr_mutex_lock(&msd->mutex);
    if (csw_request->status == NO_ERROR) {
        list_add_tail(&msd->free_csw_reqs, &csw_request->node);
    } else {
        list_add_head(&msd->queued_reqs, &csw_request->node);
    }
    ums_next_request(msd);

    // TODO: handle error case for CSW by setting iotxn to error and returning
    list_node_t* iotxn_node = list_remove_head(&msd->queued_iotxns);
    iotxn_t* curr_txn = containerof(iotxn_node, iotxn_t, node);

    ums_pdata_t* pdata = ums_iotxn_pdata(curr_txn);
    csw_status_t csw_error = ums_verify_csw(csw_request, pdata->tag);
    if (csw_error) {
        // do something
        DEBUG_PRINT(("UMS:ENCOUNTERED ERROR!!!! CODE %d\n", csw_error));
        curr_txn->ops->complete(curr_txn, ERR_BAD_STATE, 0);
        return;
    }
    // if head of iotxn list is a read iotxn and CSW reports success, then set its buffer to that
    // of the latest read request, with limited length based on data residue field in CSW
    DEBUG_PRINT(("UMS:Got here\n"));
    if (curr_txn->opcode == IOTXN_OP_READ || pdata->is_driver_io) {
        DEBUG_PRINT(("UMS:Got in\n"));
        list_node_t* node = list_remove_head(&msd->completed_reads);
        DEBUG_PRINT(("UMS:Got node\n"));


        if (!node) {
            DEBUG_PRINT(("UMS:no read node when trying to get last read\n"));
            curr_txn->ops->complete(curr_txn, ERR_BAD_STATE, 0);
            return;
        }
        usb_request_t* read_request = containerof(node, usb_request_t, node);
        // data residue field is the 3rd uint32_t in csw buffer
        uint32_t residue = letoh32(*((uint32_t *)(csw_request->buffer) + 2));
        uint32_t length = read_request->transfer_length - residue;
        DEBUG_PRINT(("UMS: residue is %08x\n", residue));
        DEBUG_PRINT(("UMS: length is %08x\n", length));

        curr_txn->ops->copyto(curr_txn, read_request->buffer, length, 0);
        list_add_tail(&msd->free_read_reqs, node);
    }
    curr_txn->ops->complete(curr_txn, NO_ERROR, curr_txn->length);
    mxr_mutex_unlock(&msd->mutex);
    DEBUG_PRINT(("UMS:ENDING CSW COMPLETE\n"));
}

static void ums_write_complete(usb_request_t* request) {
    DEBUG_PRINT(("UMS:STARTING WRITE COMPLETE\n"));
    ums_t* msd = (ums_t*)request->client_data;
    // FIXME what to do with error here?
    mxr_mutex_lock(&msd->mutex);
    list_add_tail(&msd->free_write_reqs, &request->node);
    ums_next_request(msd);
    mxr_mutex_unlock(&msd->mutex);
    DEBUG_PRINT(("UMS:ENDING WRITE COMPLETE\n"));
}


mx_status_t ums_inquiry(mx_device_t* device, uint8_t lun,  uint32_t tag) {
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_INQUIRY_COMMAND_LENGTH];
    memset(command, 0, MS_INQUIRY_COMMAND_LENGTH);
    // set command type
    command[0] = MS_INQUIRY;
    // set allocated length in scsi command
    command[4] = MS_INQUIRY_TRANSFER_LENGTH;
    status = ums_send_cbw(msd, tag, MS_INQUIRY_TRANSFER_LENGTH, USB_DIR_IN, lun,
                          MS_INQUIRY_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read inquiry response
    status = ums_queue_read(msd, MS_INQUIRY_TRANSFER_LENGTH);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_test_unit_ready(mx_device_t* device, uint8_t lun,  uint32_t tag) {
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_TEST_UNIT_READY_COMMAND_LENGTH];
    memset(command, 0, MS_TEST_UNIT_READY_COMMAND_LENGTH);
    // set command type
    command[0] = (char)MS_TEST_UNIT_READY;
    status = ums_send_cbw(msd, tag, MS_NO_TRANSFER_LENGTH, USB_DIR_IN, lun,
                          MS_TEST_UNIT_READY_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_request_sense(mx_device_t* device, uint8_t lun,  uint32_t tag) {
    DEBUG_PRINT(("UMS:starting request sense\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_REQUEST_SENSE_COMMAND_LENGTH];
    memset(command, 0, MS_REQUEST_SENSE_COMMAND_LENGTH);
    // set command type
    command[0] = MS_REQUEST_SENSE;
    // set allocated length in scsi command
    command[4] = MS_REQUEST_SENSE_TRANSFER_LENGTH;
    status = ums_send_cbw(msd, tag, MS_REQUEST_SENSE_TRANSFER_LENGTH, USB_DIR_IN, lun,
                            MS_REQUEST_SENSE_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_read(msd, MS_REQUEST_SENSE_TRANSFER_LENGTH);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read_format_capacities(mx_device_t* device, uint8_t lun,  uint32_t tag) {
    DEBUG_PRINT(("UMS:starting read format capacities\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_READ_FORMAT_CAPACITIES_COMMAND_LENGTH];
    memset(command, 0, MS_READ_FORMAT_CAPACITIES_COMMAND_LENGTH);
    // set command type
    command[0] = MS_READ_FORMAT_CAPACITIES;
    // set allocated length in scsi command
    command[8] = MS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH;
    status = ums_send_cbw(msd, tag, MS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH, USB_DIR_IN, lun,
                            MS_READ_FORMAT_CAPACITIES_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_read(msd, MS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read_capacity10(mx_device_t* device, uint8_t lun,  uint32_t tag) {
    DEBUG_PRINT(("UMS:starting read capacity10\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_READ_CAPACITY10_COMMAND_LENGTH];
    memset(command, 0, MS_READ_CAPACITY10_COMMAND_LENGTH);
    // set command type
    command[0] = MS_READ_CAPACITY10;
    status = ums_send_cbw(msd, tag, MS_READ_CAPACITY10_TRANSFER_LENGTH, USB_DIR_IN, lun,
                            MS_READ_CAPACITY10_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read capacity10 response
    status = ums_queue_read(msd, MS_READ_CAPACITY10_TRANSFER_LENGTH);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read_capacity16(mx_device_t* device, uint8_t lun,  uint32_t tag) {
    DEBUG_PRINT(("UMS:starting request sense\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_READ_CAPACITY16_COMMAND_LENGTH];
    memset(command, 0, MS_READ_CAPACITY16_COMMAND_LENGTH);
    // set command type
    command[0] = MS_READ_CAPACITY16;
    // service action = 10, not sure what that means
    command[1] = 0x10;
    status = ums_send_cbw(msd, tag, MS_READ_CAPACITY16_TRANSFER_LENGTH, USB_DIR_IN, lun,
                            MS_READ_CAPACITY16_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_read(msd, MS_READ_CAPACITY16_TRANSFER_LENGTH);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read10(mx_device_t* device, uint8_t lun, uint32_t tag, uint32_t lba, uint16_t num_blocks) {
    DEBUG_PRINT(("UMS:starting read10\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = num_blocks * msd->block_size;
    uint8_t command[MS_READ10_COMMAND_LENGTH];
    memset(command, 0, MS_READ10_COMMAND_LENGTH);
    // set command type
    command[0] = MS_READ10;
    // set lba
    write32be(command + 2, lba);
    // set transfer length in blocks
    write16be(command + 7, num_blocks);

    status = ums_send_cbw(msd, tag, transfer_length, USB_DIR_IN, lun, MS_READ10_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_read(msd, transfer_length);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read12(mx_device_t* device, uint8_t lun, uint32_t tag, uint32_t lba, uint32_t num_blocks) {
    DEBUG_PRINT(("UMS:starting read12\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = num_blocks * msd->block_size;
    uint8_t command[MS_READ12_COMMAND_LENGTH];
    memset(command, 0, MS_READ12_COMMAND_LENGTH);
    // set command type
    command[0] = MS_READ12;
    // set lba
    write32be(command + 2, lba);
    // set transfer length in blocks
    write32be(command + 6, num_blocks);

    status = ums_send_cbw(msd, tag, transfer_length, USB_DIR_IN, lun,
        MS_READ12_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_read(msd, transfer_length);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read16(mx_device_t* device, uint8_t lun, uint32_t tag, uint64_t lba, uint32_t num_blocks) {
    DEBUG_PRINT(("UMS:starting read16\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = num_blocks * msd->block_size;
    uint8_t command[MS_READ16_COMMAND_LENGTH];
    memset(command, 0, MS_READ16_COMMAND_LENGTH);
    // set command type
    command[0] = MS_READ16;
    // set lba
    write64be(command + 2, lba);
    // set transfer length in blocks
    write32be(command + 10, num_blocks);

    status = ums_send_cbw(msd, tag, transfer_length, USB_DIR_IN, lun,
                            MS_READ16_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_read(msd, transfer_length);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_write10(mx_device_t* device, uint8_t lun, uint32_t tag, uint32_t lba, uint16_t num_blocks,
                        iotxn_t* txn) {
    DEBUG_PRINT(("UMS:starting write10\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = num_blocks * msd->block_size;
    uint8_t command[MS_WRITE10_COMMAND_LENGTH];
    memset(command, 0, MS_WRITE10_COMMAND_LENGTH);
    // set command type
    command[0] = MS_WRITE10;
    // set lba
    uint32_t* lba_ptr = (uint32_t*)&(command[2]);
    *lba_ptr = htobe32(lba);
    // set transfer length in blocks
    uint16_t* transfer_len_ptr = (uint16_t*)&(command[7]);
    *transfer_len_ptr = htobe16(num_blocks);
    status = ums_send_cbw(msd, tag, transfer_length, USB_DIR_OUT, lun,
                            MS_WRITE10_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_write(msd, transfer_length, txn);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_write12(mx_device_t* device, uint8_t lun, uint32_t tag, uint32_t lba, uint32_t num_blocks,
                        iotxn_t* txn) {
    DEBUG_PRINT(("UMS:starting write12\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = num_blocks * msd->block_size;
    uint8_t command[MS_WRITE12_COMMAND_LENGTH];
    memset(command, 0, MS_WRITE12_COMMAND_LENGTH);
    // set command type
    command[0] = MS_WRITE12;
    // set lba
    uint32_t* lba_ptr = (uint32_t*)&(command[2]);
    *lba_ptr = htobe32(lba);
    // set transfer length
    uint32_t* transfer_len_ptr = (uint32_t*)&(command[7]);
    *transfer_len_ptr = htobe32(num_blocks);
    status = ums_send_cbw(msd, tag, transfer_length, USB_DIR_OUT, lun,
                            MS_WRITE12_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_write(msd, transfer_length, txn);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_write16(mx_device_t* device, uint8_t lun, uint32_t tag, uint64_t lba, uint32_t num_blocks,
                        iotxn_t* txn) {
    DEBUG_PRINT(("UMS:starting write16\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint32_t transfer_length = num_blocks * msd->block_size;
    uint8_t command[MS_WRITE16_COMMAND_LENGTH];
    memset(command, 0, MS_WRITE16_COMMAND_LENGTH);
    // set command type
    command[0] = MS_WRITE16;
    // set lba
    uint64_t* lba_ptr = (uint64_t*)&(command[2]);
    *lba_ptr = htobe64(lba);
    // set transfer length
    uint32_t* transfer_len_ptr = (uint32_t*)&(command[7]);
    *transfer_len_ptr = htobe32(num_blocks);
    status = ums_send_cbw(msd, tag, transfer_length, USB_DIR_OUT, lun,
                            MS_WRITE16_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_write(msd, transfer_length, txn);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_toggle_removable(mx_device_t* device, uint8_t lun, uint32_t tag, bool removable) {
    DEBUG_PRINT(("UMS:starting toggle removable\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_TOGGLE_REMOVABLE_COMMAND_LENGTH];
    memset(command, 0, MS_TOGGLE_REMOVABLE_COMMAND_LENGTH);
    // set command type
    command[0] = MS_TOGGLE_REMOVABLE;
    status = ums_send_cbw(msd, tag, MS_NO_TRANSFER_LENGTH, USB_DIR_OUT, lun,
                            MS_TOGGLE_REMOVABLE_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

static mx_status_t ums_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t ums_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t ums_release(mx_device_t* device) {
    ums_t* msd = get_ums(device);
    free(msd);
    return NO_ERROR;
}

static void ums_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    DEBUG_PRINT(("UMS: start of iotxn_queue. txn->length = 0x%02x\n", (uint32_t)txn->length));

    ums_t* msd = get_ums(dev);
    ums_pdata_t* pdata = ums_iotxn_pdata(txn);
    pdata->tag = msd->tag;
    msd->tag += 1;


    list_add_tail(&msd->queued_iotxns, &txn->node);

    if(pdata->is_driver_io){
        switch (pdata->cmd) {
        case MS_READ_CAPACITY10:
            ums_read_capacity10(dev, 0, pdata->tag);
            break;
        case MS_READ_CAPACITY16:
            ums_read_capacity16(dev, 0, pdata->tag);
            break;
        case MS_INQUIRY:
            ums_inquiry(dev, 0, pdata->tag);
            break;
        case MS_TEST_UNIT_READY:
            ums_test_unit_ready(dev, 0, pdata->tag);
            break;
        case MS_REQUEST_SENSE:
            ums_request_sense(dev, 0, pdata->tag);
            break;
        }
        DEBUG_PRINT(("UMS:end of is_driver_io, exiting iotxn_queue\n"));
        return;
    }


    uint32_t block_size = msd->block_size;

    // offset must be aligned to block size
    if (txn->offset % block_size) {
        DEBUG_PRINT(("UMS:offset on iotxn not aligned to block size\n"));
        txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    if (txn->length % block_size) {
        DEBUG_PRINT(("UMS:length on iotxn not aligned to block size\n"));
        txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    if (txn->opcode == IOTXN_OP_READ){
        // TODO: deal with lun
        uint8_t lun = 0;
        switch (msd->read_flag) {
        case USE_READ10:
            // is this proper way to check for exceeding size of expected type?
            // NO USE SIZEOF CHECK, SEE IOCTL FUNCTION
            // if(txn->offset > 0xFFFF) {
            //     DEBUG_PRINT(("UMS:txn offset too large, read10 only takes uint32_t\n"));
            // }
            // if(txn->length > 0xFF) {
            //     DEBUG_PRINT(("UMS:txn length too large, read10 only takes uint16_t\n"));
            // }
            ums_read10(dev, lun, pdata->tag, txn->offset/block_size, txn->length/block_size);
            break;
        case USE_READ12:
            // if(txn->offset > 0xFFFF) {
            //     DEBUG_PRINT(("UMS:txn offset too large, read12 only takes uint32_t\n"));
            // }
            // if(txn->length > 0xFFFF) {
            //     DEBUG_PRINT(("UMS:txn length too large, read12 only takes uint32_t\n"));
            // }
            ums_read12(dev, lun, pdata->tag, txn->offset/block_size, txn->length/block_size);
            break;
        case USE_READ16:
            ums_read16(dev, lun, pdata->tag, txn->offset/block_size, txn->length/block_size);
            break;
        }
        // DEBUG_PRINT(("UMS:data: %p\n", data));
        // TODO: figure out how to make these prints print out bigger stuff
        DEBUG_PRINT(("UMS: starting read. read len: %d; read offset: %d both in bytes\n", (uint8_t)txn->length, (uint8_t)txn->offset));
    }else if (txn->opcode == IOTXN_OP_WRITE){

        // TODO: deal with lun
        uint8_t lun = 0;
        DEBUG_PRINT(("UMS: starting write. txn->length = 0x%02x num_blocks = 0x%02x\n", (uint32_t)txn->length, (uint32_t)txn->length/block_size));

        switch (msd->read_flag) {

        case USE_READ10:
            ums_write10(dev, lun, pdata->tag, txn->offset/block_size, txn->length/block_size, txn);
            break;
        case USE_READ12:
            ums_write12(dev, lun, pdata->tag, txn->offset/block_size, txn->length/block_size, txn);
            break;
        case USE_READ16:
            ums_write16(dev, lun, pdata->tag, txn->offset/block_size, txn->length/block_size, txn);
            break;
        }
        // DEBUG_PRINT(("UMS:data: %p\n", data));
        // TODO: figure out how to make these prints print out bigger stuff
        DEBUG_PRINT(("UMS: starting write. read len: %d; read offset: %d both in bytes\n", (uint8_t)txn->length, (uint8_t)txn->offset));
    }
}

static ssize_t ums_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    ums_t* msd = get_ums(dev);
    // TODO implement other block ioctls
    switch (op) {
    case BLOCK_OP_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size)) return ERR_NOT_ENOUGH_BUFFER;
        *size = msd->total_blocks;
        return sizeof(*size);
    }
    case BLOCK_OP_GET_BLOCKSIZE: {
         uint64_t* blksize = reply;
         if (max < sizeof(*blksize)) return ERR_NOT_ENOUGH_BUFFER;
         *blksize = msd->block_size;
         return sizeof(*blksize);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_off_t ums_get_size(mx_device_t* dev) {
    ums_t* msd = get_ums(dev);
    return msd->block_size * msd->total_blocks;
}

static mx_protocol_device_t ums_device_proto = {
    // .read = ums_read,
    // .write = ums_write,
    .ioctl = ums_ioctl,
    .release = ums_release,
    .iotxn_queue = ums_iotxn_queue,
    .get_size = ums_get_size,
};

static void ums_iotxn_wait_cb(iotxn_t* txn) {
    DEBUG_PRINT(("UMS:iotxn_wait_cb called\n"));
    ums_pdata_t* pdata = ums_iotxn_pdata(txn);
    completion_signal(pdata->waiter);
    completion_reset(pdata->waiter);
    return;
}

static int ums_start_thread(void* arg) {
    ums_t* msd = (ums_t*)arg;

    mx_status_t status = device_init(&msd->device, msd->driver, "usb_mass_storage", &ums_device_proto);
    if (status != NO_ERROR) {
        free(msd);
        return status;
    }
    DEBUG_PRINT(("UMS:starting start_thread\n"));
    iotxn_t* txn;
    iotxn_alloc(&txn, 0, MS_READ_CAPACITY10_TRANSFER_LENGTH, 0);
    //TODO: what op goes into txn? not sure if this is proper style but putting -1 here to show its not a std op?
    txn->opcode = -1;
    ums_pdata_t* pdata = ums_iotxn_pdata(txn);
    pdata->is_driver_io = true;
    pdata->cmd = MS_READ_CAPACITY10;
    pdata->waiter = &(msd->read_completion);
    txn->complete_cb = ums_iotxn_wait_cb;
    ums_iotxn_queue(&msd->device, txn);
    // ums_read_capacity10(&(msd->device), 0);
    completion_wait(&(msd->read_completion), MX_TIME_INFINITE);
    DEBUG_PRINT(("UMS:in start thread, got past wait\n"));

    uint32_t read_capacity[MS_READ_CAPACITY10_TRANSFER_LENGTH];
    txn->ops->copyfrom(txn, (void*)read_capacity, MS_READ_CAPACITY10_TRANSFER_LENGTH, 0);
    // +1 because this returns the address of the final block, and blocks are zero indexed
    msd->total_blocks = betoh32(read_capacity[0]) + 1;
    msd->block_size = betoh32(read_capacity[1]);
    msd->read_flag = USE_READ10;
    if (read_capacity[0] == 0xFFFFFFFF) {
        iotxn_t* txn2;
        iotxn_alloc(&txn2, 0, MS_READ_CAPACITY16_TRANSFER_LENGTH, 0);
        txn2->opcode = -1;
        ums_pdata_t* pdata2 = ums_iotxn_pdata(txn2);
        pdata2->is_driver_io = true;
        pdata2->cmd = MS_READ_CAPACITY16;
        pdata2->waiter = &(msd->read_completion);
        txn2->complete_cb = ums_iotxn_wait_cb;
        ums_iotxn_queue(&msd->device, txn2);
        completion_wait(&(msd->read_completion), MX_TIME_INFINITE);
        char read_capacity2[MS_READ_CAPACITY16_TRANSFER_LENGTH];
        txn2->ops->copyfrom(txn2, (void*)read_capacity2, MS_READ_CAPACITY16_TRANSFER_LENGTH, 0);
        //FIXME whats the best way to unpack a uint64 from start of the buffer and then a 32 directly after that?
        // msd->total_blocks = betoh64(*((uint64_t*)&read_capacity2));
        // msd->block_size = betoh32(*((uint32_t*)&read_capacity2 + 8));
        msd->read_flag = USE_READ12;
    }
    DEBUG_PRINT(("UMS:block size is: 0x%08x\n", msd->block_size));
    DEBUG_PRINT(("UMS:total blocks is: %ld\n", (long)msd->total_blocks));
    msd->device.protocol_id = MX_PROTOCOL_BLOCK;
    device_add(&msd->device, msd->udev);
    DEBUG_PRINT(("UMS:reached end of start thread\n"));
    return NO_ERROR;
}

static mx_status_t ums_bind(mx_driver_t* driver, mx_device_t* device) {
    DEBUG_PRINT(("UMS:starting mass storage probe\n"));
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
        DEBUG_PRINT(("UMS:ums_bind wrong number of endpoints: %d\n", intf->num_endpoints));
        return ERR_NOT_SUPPORTED;
    }
    usb_endpoint_t* bulk_in = NULL;
    usb_endpoint_t* bulk_out = NULL;

    for (int i = 0; i < intf->num_endpoints; i++) {
        usb_endpoint_t* endp = &intf->endpoints[i];
        if (endp->direction == USB_ENDPOINT_OUT) {
            if (endp->type == USB_ENDPOINT_BULK) {
                bulk_out = endp;
            }
        } else {
            if (endp->type == USB_ENDPOINT_BULK) {
                bulk_in = endp;
            } else if (endp->type == USB_ENDPOINT_INTERRUPT) {
                DEBUG_PRINT(("UMS:BULK INTERRUPT ENDPOINT FOUND\n"));
            }
        }
    }
    if (!bulk_in || !bulk_out) {
        DEBUG_PRINT(("UMS:ums_bind could not find endpoints\n"));
        return ERR_NOT_SUPPORTED;
    }

    ums_t* msd = calloc(1, sizeof(ums_t));
    if (!msd) {
        DEBUG_PRINT(("UMS:Not enough memory for ums_t\n"));
        return ERR_NO_MEMORY;
    }

    list_initialize(&msd->free_read_reqs);
    list_initialize(&msd->free_csw_reqs);
    list_initialize(&msd->free_write_reqs);
    list_initialize(&msd->queued_reqs);
    list_initialize(&msd->queued_iotxns);
    list_initialize(&msd->completed_reads);
    list_initialize(&msd->completed_csws);

    msd->udev = device;
    msd->driver = driver;
    msd->usb_p = protocol;
    msd->bulk_in = bulk_in;
    msd->bulk_out = bulk_out;

    for (int i = 0; i < READ_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, bulk_in, USB_BUF_SIZE);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = ums_read_complete;
        req->client_data = msd;
        list_add_head(&msd->free_read_reqs, &req->node);
    }
    for (int i = 0; i < READ_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, bulk_in, MSD_COMMAND_STATUS_WRAPPER_SIZE);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = ums_csw_complete;
        req->client_data = msd;
        list_add_head(&msd->free_csw_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, bulk_out, USB_BUF_SIZE);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = ums_write_complete;
        req->client_data = msd;
        list_add_head(&msd->free_write_reqs, &req->node);
    }

    char lun = 'a';
    ums_get_max_lun(msd, (void*)&lun);
    DEBUG_PRINT(("UMS:Max lun is: %02x\n", (unsigned char)lun));

    msd->busy = false;
    msd->tag = 8;
    msd->read_completion = COMPLETION_INIT;
    mxr_thread_t* thread;
    mxr_thread_create(ums_start_thread, msd, "ums_start_thread", &thread);
    mxr_thread_detach(thread);

    DEBUG_PRINT(("UMS:mass storage bind complete\n"));
    return NO_ERROR;
}

static mx_status_t ums_unbind(mx_driver_t* drv, mx_device_t* dev) {
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
        .bind = ums_bind,
        .unbind = ums_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
