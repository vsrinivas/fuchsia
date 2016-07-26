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
#include <runtime/completion.h>
#include <runtime/mutex.h>
#include <runtime/thread.h>
#include <system/listnode.h>

#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ums-hw.h"

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define INTR_REQ_COUNT 4
#define USB_BUF_SIZE 0x8000
#define INTR_REQ_SIZE 8
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
    mx_device_t* usb_device;
    usb_device_protocol_t* device_protocol;
    mx_driver_t* driver;

    bool busy;
    uint8_t tag;
    uint32_t total_blocks;
    uint32_t block_size;
    uint8_t capacity_descriptor;
    uint8_t read_flag;

    usb_endpoint_t* bulk_in;
    usb_endpoint_t* bulk_out;
    usb_endpoint_t* intr_ep;

    // pool of free USB requests
    list_node_t free_csw_reqs;
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
    mxr_completion_t read_completion;

} ums_t;
#define get_ums(dev) containerof(dev, ums_t, device)

static mx_status_t ums_reset(ums_t* msd) {
    // for all these control requests, data is null, length is 0 because nothing is passed back
    // value and index not used for first command, though index is supposed to be set to interface number
    // TODO: check interface number, see if index needs to be set
    mx_status_t status = msd->device_protocol->control(msd->usb_device, USB_DIR_OUT | USB_TYPE_CLASS
                                            | USB_RECIP_INTERFACE, USB_REQ_RESET, 0x00, 0x00, NULL, 0);
    DEBUG_PRINT(("resetting, status is: %d\n", (int)status));
    status = msd->device_protocol->control(msd->usb_device, USB_DIR_OUT | USB_TYPE_CLASS
                                           | USB_RECIP_INTERFACE, USB_REQ_CLEAR_FEATURE, FS_ENDPOINT_HALT,
                                           msd->bulk_in->endpoint, NULL, 0);
    DEBUG_PRINT(("halting in, status is: %d\n", (int)status));
    status = msd->device_protocol->control(msd->usb_device, USB_DIR_OUT | USB_TYPE_CLASS
                                           | USB_RECIP_INTERFACE, USB_REQ_CLEAR_FEATURE, FS_ENDPOINT_HALT,
                                           msd->bulk_out->endpoint, NULL, 0);
    DEBUG_PRINT(("halting out, status is: %d\n", (int)status));
    return status;
}

static mx_status_t ums_get_max_lun(ums_t* msd, void* data) {
    mx_status_t status = msd->device_protocol->control(msd->usb_device, USB_DIR_IN | USB_TYPE_CLASS
                                    | USB_RECIP_INTERFACE, USB_REQ_GET_MAX_LUN, 0x00, 0x00, data, 1);
    DEBUG_PRINT(("getting max lun, status is: %d\n", (int)status));
    return status;
}

static mx_status_t ums_get_endpoint_status(ums_t* msd, usb_endpoint_t* endpoint, void* data) {
    mx_status_t status = msd->device_protocol->control(msd->usb_device, USB_DIR_IN | USB_TYPE_CLASS
                                    | USB_RECIP_INTERFACE, USB_REQ_GET_STATUS, 0x00,
                                    endpoint->endpoint, data, 2);
    DEBUG_PRINT(("getting endpoint status, status is: %d\n", (int)status));
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
    DEBUG_PRINT(("in queue request\n"));
    if (!(msd->busy)) {
        DEBUG_PRINT(("not busy case\n"));
        msd->busy = true;
        return msd->device_protocol->queue_request(msd->usb_device, request);
    } else {
        DEBUG_PRINT(("busy case\n"));
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
    memcpy(&(ptr_8[15]), command, (size_t)command_len);
    return ums_queue_request(msd, request);
}

static mx_status_t ums_recv_csw(ums_t* msd) {
    list_node_t* csw_node = list_remove_head(&msd->free_csw_reqs);
    if (!csw_node) {
        return ERR_NOT_ENOUGH_BUFFER;
    }
    usb_request_t* csw_request = containerof(csw_node, usb_request_t, node);
    csw_request->transfer_length = MSD_COMMAND_STATUS_WRAPPER_SIZE;
    memset(csw_request->buffer, 0, csw_request->transfer_length);
    DEBUG_PRINT(("queued read request\n"));
    return ums_queue_request(msd, csw_request);
}

static mx_status_t ums_queue_read(ums_t* msd, uint16_t transfer_length) {
    // read request sense response
    list_node_t* read_node = list_remove_head(&msd->free_read_reqs);
    if (!read_node) {
        return ERR_NOT_ENOUGH_BUFFER;
    }
    usb_request_t* read_request = containerof(read_node, usb_request_t, node);
    read_request->transfer_length = transfer_length;
    return ums_queue_request(msd, read_request);
}

static mx_status_t ums_queue_write(ums_t* msd, uint16_t transfer_length, const void* data) {
    list_node_t* write_node = list_remove_head(&msd->free_write_reqs);
    if (!write_node) {
        return ERR_NOT_ENOUGH_BUFFER;
    }
    usb_request_t* write_request = containerof(write_node, usb_request_t, node);
    write_request->transfer_length = transfer_length;
    memcpy(write_request, data, (size_t)transfer_length);
    return ums_queue_request(msd, write_request);
}

// return true if valid CSW, else false
// maybe called process csw?
static csw_status_t ums_verify_csw(usb_request_t* csw_request, usb_request_t* data_request,
                                  uint32_t prevtag) {
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
    if (ptr_8[12] == CSW_FAILED) {
        return CSW_FAILED;
    } else if (ptr_8[12] == CSW_PHASE_ERROR) {
        return CSW_PHASE_ERROR;
    }
    // if yes then success, and check data residue, modify transfer length so valid data only is read
    data_request->transfer_length -= letoh32(ptr_32[2]);
    // to complete read method so that completed read's transfer length is changed to appropriate
    return CSW_SUCCESS;
}

static mx_status_t ums_next_request(ums_t* msd) {
    DEBUG_PRINT(("Trying to dequeue next request\n"));
    DEBUG_PRINT(("number of requests in list: %d\n", (int)(list_length(&msd->queued_reqs))));
    list_node_t* node = list_remove_head(&msd->queued_reqs);
    if (!node) {
        DEBUG_PRINT(("no more nodes\n"));
        msd->busy = false;
        return 0;
    }
    DEBUG_PRINT(("got node, queuing request\n"));
    usb_request_t* request = containerof(node, usb_request_t, node);
    mx_status_t status = msd->device_protocol->queue_request(msd->usb_device, request);
    return status;
}

static void update_signals_locked(ums_t* msd) {
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

static void ums_read_complete(usb_request_t* request) {
    DEBUG_PRINT(("STARTING READ COMPLETE\n"));

    ums_t* msd = (ums_t*)request->client_data;

    mxr_mutex_lock(&msd->mutex);
    if (request->status == NO_ERROR) {
        list_add_tail(&msd->completed_reads, &request->node);
        mxr_completion_signal(&(msd->read_completion));
    } else {
        list_add_head(&msd->queued_reqs, &request->node);
    }
    ums_next_request(msd);
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    DEBUG_PRINT(("ENDING READ COMPLETE\n"));
}

static void ums_csw_complete(usb_request_t* request) {
    DEBUG_PRINT(("STARTING CSW COMPLETE\n"));

    ums_t* msd = (ums_t*)request->client_data;

    mxr_mutex_lock(&msd->mutex);
    if (request->status == NO_ERROR) {
        //TODO: verify csw against info
        list_add_tail(&msd->free_csw_reqs, &request->node);
    } else {
        list_add_head(&msd->queued_reqs, &request->node);
    }
    ums_next_request(msd);
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    DEBUG_PRINT(("ENDING CSW COMPLETE\n"));
}

static void ums_write_complete(usb_request_t* request) {
    DEBUG_PRINT(("STARTING WRITE COMPLETE\n"));
    ums_t* msd = (ums_t*)request->client_data;
    // FIXME what to do with error here?
    mxr_mutex_lock(&msd->mutex);
    list_add_tail(&msd->free_write_reqs, &request->node);
    ums_next_request(msd);
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    DEBUG_PRINT(("ENDING WRITE COMPLETE\n"));
}

static void ums_interrupt_complete(usb_request_t* request) {
    DEBUG_PRINT(("INTERRUPT HAPPENING?\n"));
    ums_t* msd = (ums_t*)request->client_data;
    mxr_mutex_lock(&msd->mutex);
    update_signals_locked(msd);
    list_add_head(&msd->free_intr_reqs, &request->node);
    mxr_mutex_unlock(&msd->mutex);
}

mx_status_t ums_inquiry(mx_device_t* device, uint8_t lun) {
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
    status = ums_send_cbw(msd, (msd->tag)++, MS_INQUIRY_TRANSFER_LENGTH, USB_DIR_IN, lun,
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
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_test_unit_ready(mx_device_t* device, uint8_t lun) {
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_TEST_UNIT_READY_COMMAND_LENGTH];
    memset(command, 0, MS_TEST_UNIT_READY_COMMAND_LENGTH);
    // set command type
    command[0] = (char)MS_TEST_UNIT_READY;
    status = ums_send_cbw(msd, (msd->tag)++, MS_NO_TRANSFER_LENGTH, USB_DIR_IN, lun,
                          MS_TEST_UNIT_READY_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_request_sense(mx_device_t* device, uint8_t lun) {
    DEBUG_PRINT(("starting request sense\n"));
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
    status = ums_send_cbw(msd, (msd->tag)++, MS_REQUEST_SENSE_TRANSFER_LENGTH, USB_DIR_IN, lun,
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
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read_format_capacities(mx_device_t* device, uint8_t lun) {
    DEBUG_PRINT(("starting request sense\n"));
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
    status = ums_send_cbw(msd, (msd->tag)++, MS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH, USB_DIR_IN, lun,
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
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read_capacity10(mx_device_t* device, uint8_t lun) {
    DEBUG_PRINT(("starting request sense\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_READ_CAPACITY10_COMMAND_LENGTH];
    memset(command, 0, MS_READ_CAPACITY10_COMMAND_LENGTH);
    // set command type
    command[0] = MS_READ_CAPACITY10;
    status = ums_send_cbw(msd, (msd->tag)++, MS_READ_CAPACITY10_TRANSFER_LENGTH, USB_DIR_IN, lun,
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
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read_capacity16(mx_device_t* device, uint8_t lun) {
    DEBUG_PRINT(("starting request sense\n"));
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
    status = ums_send_cbw(msd, (msd->tag)++, MS_READ_CAPACITY16_TRANSFER_LENGTH, USB_DIR_IN, lun,
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
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read10(mx_device_t* device, uint8_t lun, uint32_t lba, uint16_t num_blocks) {
    DEBUG_PRINT(("starting read10\n"));
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
    uint32_t* lba_ptr = (uint32_t*)&(command[2]);
    *lba_ptr = htobe32(lba);
    // set transfer length in blocks
    uint16_t* transfer_len_ptr = (uint16_t*)&(command[7]);
    *transfer_len_ptr = htobe16(num_blocks);
    status = ums_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun, MS_READ10_COMMAND_LENGTH, command);
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
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read12(mx_device_t* device, uint8_t lun, uint32_t lba, uint32_t num_blocks) {
    DEBUG_PRINT(("starting read12\n"));
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
    uint32_t* lba_ptr = (uint32_t*)&(command[2]);
    *lba_ptr = htobe32(lba);
    // set transfer length
    uint32_t* transfer_len_ptr = (uint32_t*)&(command[7]);
    *transfer_len_ptr = htobe32(num_blocks);
    status = ums_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun,
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
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_read16(mx_device_t* device, uint8_t lun, uint64_t lba, uint32_t num_blocks) {
    DEBUG_PRINT(("starting read16\n"));
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
    uint64_t* lba_ptr = (uint64_t*)&(command[2]);
    *lba_ptr = htobe64(lba);
    // set transfer length
    uint32_t* transfer_len_ptr = (uint32_t*)&(command[7]);
    *transfer_len_ptr = htobe32(num_blocks);
    status = ums_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_IN, lun,
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
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_write10(mx_device_t* device, uint8_t lun, uint32_t lba, uint16_t num_blocks,
                        const void* data) {
    DEBUG_PRINT(("starting write10\n"));
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
    status = ums_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_OUT, lun,
                            MS_WRITE10_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_write(msd, transfer_length, data);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_write12(mx_device_t* device, uint8_t lun, uint32_t lba, uint32_t num_blocks,
                        const void* data) {
    DEBUG_PRINT(("starting write12\n"));
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
    status = ums_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_OUT, lun,
                            MS_WRITE12_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_write(msd, transfer_length, data);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_write16(mx_device_t* device, uint8_t lun, uint64_t lba, uint32_t num_blocks,
                        const void* data) {
    DEBUG_PRINT(("starting write16\n"));
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
    status = ums_send_cbw(msd, (msd->tag)++, transfer_length, USB_DIR_OUT, lun,
                            MS_WRITE16_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // read request sense response
    status = ums_queue_write(msd, transfer_length, data);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_toggle_removable(mx_device_t* device, uint8_t lun, bool removable) {
    DEBUG_PRINT(("starting toggle removable\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&msd->mutex);

    // CBW Configuration
    uint8_t command[MS_TOGGLE_REMOVABLE_COMMAND_LENGTH];
    memset(command, 0, MS_TOGGLE_REMOVABLE_COMMAND_LENGTH);
    // set command type
    command[0] = MS_TOGGLE_REMOVABLE;
    status = ums_send_cbw(msd, (msd->tag)++, MS_NO_TRANSFER_LENGTH, USB_DIR_OUT, lun,
                            MS_TOGGLE_REMOVABLE_COMMAND_LENGTH, command);
    if (status == ERR_NOT_ENOUGH_BUFFER) {
        goto out;
    }

    // recieve CSW
    status = ums_recv_csw(msd);
out:
    update_signals_locked(msd);
    mxr_mutex_unlock(&msd->mutex);
    return status;
}

mx_status_t ums_recv(mx_device_t* device, void* buffer, size_t length) {
    DEBUG_PRINT(("start of regular recv\n"));
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;
    DEBUG_PRINT(("before regular recv lock\n"));
    mxr_mutex_lock(&msd->mutex);
    DEBUG_PRINT(("after regular recv lock\n"));
    // int offset = msd->read_offset;
    for (int i = 0; i < 18; i++) {
        DEBUG_PRINT(("%02x ", ((unsigned char*)(buffer))[i]));
    }
    DEBUG_PRINT(("\n"));
    DEBUG_PRINT(("Num of read reqs completed before trying to read: %d\n", (int)list_length(&msd->completed_reads)));
    DEBUG_PRINT(("Num of read reqs waiting before trying to read: %d\n", (int)list_length(&msd->free_read_reqs)));

    list_node_t* node = list_remove_head(&msd->completed_reads);
    if (!node) {
        mxr_mutex_unlock(&msd->mutex);
        DEBUG_PRINT(("before wait\n"));
        mx_status_t wait_status = mxr_completion_wait(&(msd->read_completion), MX_TIME_INFINITE);
        DEBUG_PRINT(("after wait\n"));
        mxr_mutex_lock(&msd->mutex);
        if (wait_status == ERR_TIMED_OUT) {
            DEBUG_PRINT(("no node :(\n"));
            // is this right error code to use?
            status = ERR_NOT_FOUND;
            goto out;
        }
        mxr_completion_reset(&(msd->read_completion));
        node = list_remove_head(&msd->completed_reads);
    }
    DEBUG_PRINT(("got to recv request\n"));
    usb_request_t* request = containerof(node, usb_request_t, node);
    // uint8_t* buf = request->buffer;
    DEBUG_PRINT(("\n"));
    for (int i = 0; i < 252; i++) {
        DEBUG_PRINT(("%02x ", ((unsigned char*)(request->buffer))[i]));
    }
    DEBUG_PRINT(("\n"));
    // need to change this, as some of the reads will be csws but
    //TODO: change length1 to not a constant
    // size_t length1 = request->transfer_length;
    memcpy(buffer, request->buffer, request->transfer_length);
    for (int i = 0; i < 252; i++) {
        DEBUG_PRINT(("%02x ", ((unsigned char*)(request->buffer))[i]));
    }
    DEBUG_PRINT(("\n"));
    // for (int i = 0; i < 252; i++) {
    //     DEBUG_PRINT(("%02x ", ((unsigned char*)(buf))[i]));
    // }
    // DEBUG_PRINT(("\n"));
    for (int i = 0; i < 252; i++) {
        DEBUG_PRINT(("%02x ", ((unsigned char*)(buffer))[i]));
    }
    DEBUG_PRINT(("\n"));
out:
    DEBUG_PRINT(("got to recv out\n"));
    // msd->read_offset = offset;

    update_signals_locked(msd);
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

static ssize_t ums_read(mx_device_t* dev, void* data, size_t len, mx_off_t off) {
    DEBUG_PRINT(("starting read\n"));
    uint32_t block_size = get_ums(dev)->block_size;
    if (off % block_size != 0) {
        DEBUG_PRINT(("ERROR: offset not block aligned, read returning 0 bytes\n"));
        return 0;
    }
    if (len % block_size != 0) {
        DEBUG_PRINT(("ERROR: length of read not block aligned, read returning 0 bytes\n"));
        return 0;
    }
    // TODO: deal with lun
    uint8_t lun = 0;
    switch (get_ums(dev)->read_flag) {
    case USE_READ10:
        ums_read10(dev, lun, off, len/block_size+1);
        break;
    case USE_READ12:
        ums_read12(dev, lun, off, len/block_size+1);
        break;
    case USE_READ16:
        ums_read16(dev, lun, off, len/block_size+1);
        break;
    }
    DEBUG_PRINT(("data: %p\n", data));
    DEBUG_PRINT(("len: %d\n", (uint8_t)len));
    DEBUG_PRINT(("block size: %d\n", block_size));

    for (int i = 0; i < 18; i++) {
        DEBUG_PRINT(("%02x ", ((unsigned char*)(data))[i]));
    }
    DEBUG_PRINT(("\n"));
    ums_recv(dev, data, len);
    DEBUG_PRINT(("data post recv in read\n"));
    for (int i = 0; i < 18; i++) {
        DEBUG_PRINT(("%02x ", ((unsigned char*)(data))[i]));
    }
    DEBUG_PRINT(("returning this size: %d\n", (uint32_t)(len)));
    return len;
}

static ssize_t ums_write(mx_device_t* dev, const void* data, size_t len, mx_off_t off) {
    DEBUG_PRINT(("starting write\n"));
    // TODO: deal with lun
    uint8_t lun = 0;
    uint32_t block_size = get_ums(dev)->block_size;

    if (off % block_size != 0) {
        DEBUG_PRINT(("ERROR: offset not block aligned, read returning 0 bytes\n"));
        return 0;
    }
    if (len % block_size != 0) {
        DEBUG_PRINT(("ERROR: length of read not block aligned, read returning 0 bytes\n"));
        return 0;
    }

    switch (get_ums(dev)->read_flag) {
    case USE_READ10:
        ums_write10(dev, lun, off, len/block_size+1, data);
        break;
    case USE_READ12:
        ums_write12(dev, lun, off, len/block_size+1, data);
        break;
    case USE_READ16:
        ums_write16(dev, lun, off, len/block_size+1, data);
        break;
    }
    return len;
}

static size_t ums_get_size(mx_device_t* dev) {
    ums_t* msd = get_ums(dev);
    return msd->block_size * msd->total_blocks;
>>>>>>> [wip][ums]add read and write functionality to mass storage driver
}

static mx_protocol_device_t ums_device_proto = {
    .read = ums_read,
    .write = ums_write,
    .release = ums_release,
    .get_size = ums_get_size,
};

static int ums_start_thread(void* arg) {
    ums_t* msd = (ums_t*)arg;

    mx_status_t status = device_init(&msd->device, msd->driver, "usb_mass_storage", &ums_device_proto);
    if (status != NO_ERROR) {
        free(msd);
        return status;
    }
    DEBUG_PRINT(("starting start_thread\n"));
    ums_read_capacity10(&(msd->device), 0);
    uint32_t read_capacity[MS_READ_CAPACITY10_TRANSFER_LENGTH];

    ums_recv(&(msd->device), (void*)read_capacity, MS_READ_CAPACITY10_TRANSFER_LENGTH);
    msd->total_blocks = betoh32(read_capacity[0]);
    msd->block_size = betoh32(read_capacity[1]);
    msd->read_flag = USE_READ10;
    if (read_capacity[0] == 0xFFFFFFFF) {
        ums_read_capacity16(&(msd->device), 0);
        uint32_t read_capacity[2];
        ums_recv(&(msd->device), (void*)read_capacity, 8);
        msd->total_blocks = betoh32(read_capacity[0]);
        msd->block_size = betoh32(read_capacity[1]);
        msd->read_flag = USE_READ12;
    }
    DEBUG_PRINT(("block size is: 0x%08x\n", msd->block_size));
    DEBUG_PRINT(("total blocks is: 0x%08x\n", msd->total_blocks));
    // ums_request_sense(msd, 0);

    // mxr_mutex_unlock(&msd->mutex);
    // msd->device.protocol_id = MX_PROTOCOL_USB_MASS_STORAGE;
    // msd->device.protocol_ops = &ums_proto;
    device_add(&msd->device, msd->usb_device);
    DEBUG_PRINT(("reached end of start thread\n"));
    return NO_ERROR;
}

static mx_status_t ums_bind(mx_driver_t* driver, mx_device_t* device) {
    DEBUG_PRINT(("starting mass storage probe\n"));
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
        DEBUG_PRINT(("ums_bind wrong number of endpoints: %d\n", intf->num_endpoints));
        return ERR_NOT_SUPPORTED;
    }
    usb_endpoint_t* bulk_in = NULL;
    usb_endpoint_t* bulk_out = NULL;
    usb_endpoint_t* intr_ep = NULL;

    DEBUG_PRINT(("NUM OF ENDPOINTS: %d\n", intf->num_endpoints));
    for (int i = 0; i < intf->num_endpoints; i++) {
        usb_endpoint_t* endp = &intf->endpoints[i];
        if (endp->direction == USB_ENDPOINT_OUT) {
            if (endp->type == USB_ENDPOINT_BULK) {
                DEBUG_PRINT(("HI IM A BULK OUT: %d\n", i));
                bulk_out = endp;
            }
        } else {
            if (endp->type == USB_ENDPOINT_BULK) {
                bulk_in = endp;
                DEBUG_PRINT(("HI IM A BULK IN: %d\n", i));
            } else if (endp->type == USB_ENDPOINT_INTERRUPT) {
                DEBUG_PRINT(("HI IM A BULK INTERRUPT\n"));
                intr_ep = endp;
            }
        }
    }
    if (!bulk_in || !bulk_out) {
        DEBUG_PRINT(("ums_bind could not find endpoints\n"));
        return ERR_NOT_SUPPORTED;
    }

    ums_t* msd = calloc(1, sizeof(ums_t));
    if (!msd) {
        DEBUG_PRINT(("Not enough memory for ums_t\n"));
        return ERR_NO_MEMORY;
    }

    list_initialize(&msd->free_read_reqs);
    list_initialize(&msd->free_csw_reqs);
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

    if (msd->intr_ep) {
        for (int i = 0; i < INTR_REQ_COUNT; i++) {
            usb_request_t* req = protocol->alloc_request(device, intr_ep, intr_ep->maxpacketsize);
            if (!req)
                return ERR_NO_MEMORY;
            req->complete_cb = ums_interrupt_complete;
            req->client_data = msd;
            list_add_head(&msd->free_intr_reqs, &req->node);
        }
    }

    char lun = 'a';
    ums_get_max_lun(msd, (void*)&lun);
    DEBUG_PRINT(("Max lun is: %02x\n", (unsigned char)lun));

    msd->busy = false;
    msd->tag = 8;
    msd->read_completion = MXR_COMPLETION_INIT;
    mxr_thread_t* thread;
    mxr_thread_create(ums_start_thread, msd, "ums_start_thread", &thread);
    mxr_thread_detach(thread);
    update_signals_locked(msd);

    DEBUG_PRINT(("mass storage bind complete\n"));
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
        // .probe = ums_probe,
        .bind = ums_bind,
        .unbind = ums_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};