// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <magenta/hw/usb.h>
#include <magenta/listnode.h>
#include <sync/completion.h>
#include <ddk/protocol/block.h>

#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "ums-hw.h"

#define READ_REQ_COUNT 3
#define WRITE_REQ_COUNT 3
#define USB_BUF_SIZE 0x8000

// comment the next line if you don't want debug messages
#define DEBUG 0
#ifdef DEBUG
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

// used to implement IOCTL_DEVICE_SYNC
typedef struct {
    // iotxn we are waiting to complete
    iotxn_t* iotxn;
    // completion for IOCTL_DEVICE_SYNC to wait on
    completion_t completion;
    // node for ums_t.sync_nodes list
    list_node_t node;
} ums_sync_node_t;

typedef struct {
    mx_device_t device;
    mx_device_t* udev;
    mx_driver_t* driver;

    uint32_t tag_send;      // next tag to send in CBW
    uint32_t tag_receive;   // next tag we expect to receive in CSW

    uint8_t lun;
    uint64_t total_blocks;
    uint32_t block_size;

    bool use_read_write_16; // use READ16 and WRITE16 if total_blocks > 0xFFFFFFFF

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;

    // pool of free USB requests
    list_node_t free_csw_reqs;
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;

    // list of queued io transactions
    list_node_t queued_iotxns;

    // list of active ums_sync_node_t
    list_node_t sync_nodes;

    // list of received packets not yet read by upper layer
    list_node_t completed_reads;
    list_node_t completed_csws;

    mtx_t mutex;
} ums_t;
#define get_ums(dev) containerof(dev, ums_t, device)

static void ums_csw_complete(iotxn_t* csw_request, void* cookie);
static csw_status_t ums_verify_csw(ums_t* msd, iotxn_t* csw_request);

static mx_status_t ums_reset(ums_t* msd) {
    // for all these control requests, data is null, length is 0 because nothing is passed back
    // value and index not used for first command, though index is supposed to be set to interface number
    // TODO: check interface number, see if index needs to be set
    DEBUG_PRINT(("UMS: performing reset recovery\n"));
    mx_status_t status = usb_control(msd->udev, USB_DIR_OUT | USB_TYPE_CLASS
                                            | USB_RECIP_INTERFACE, USB_REQ_RESET, 0x00, 0x00, NULL, 0);
    status = usb_control(msd->udev, USB_DIR_OUT | USB_TYPE_CLASS
                                           | USB_RECIP_INTERFACE, USB_REQ_CLEAR_FEATURE, FS_ENDPOINT_HALT,
                                           msd->bulk_in_addr, NULL, 0);
    status = usb_control(msd->udev, USB_DIR_OUT | USB_TYPE_CLASS
                                           | USB_RECIP_INTERFACE, USB_REQ_CLEAR_FEATURE, FS_ENDPOINT_HALT,
                                           msd->bulk_out_addr, NULL, 0);
    return status;
}

static mx_status_t ums_get_max_lun(ums_t* msd, void* data) {
    mx_status_t status = usb_control(msd->udev, USB_DIR_IN | USB_TYPE_CLASS
                                    | USB_RECIP_INTERFACE, USB_REQ_GET_MAX_LUN, 0x00, 0x00, data, 1);
    return status;
}

static iotxn_t* get_free_write(ums_t* msd) {
    list_node_t* node = list_remove_head(&msd->free_write_reqs);
    if (!node) {
        return NULL;
    }
    return containerof(node, iotxn_t, node);
}

static void ums_queue_request(ums_t* msd, iotxn_t* txn) {
    iotxn_queue(msd->udev, txn);
}

static mx_status_t ums_send_cbw(ums_t* msd, uint32_t transfer_length, uint8_t flags,
                                uint8_t command_len, void* command) {
    iotxn_t* txn = get_free_write(msd);
    if (!txn) {
        return ERR_BUFFER_TOO_SMALL;
    }

    ums_cbw_t* cbw;
    iotxn_mmap(txn, (void **)&cbw);
    txn->length = sizeof(*cbw);

    memset(cbw, 0, sizeof(*cbw));
    cbw->dCBWSignature = htole32(CBW_SIGNATURE);
    cbw->dCBWTag = htole32(msd->tag_send++);
    cbw->dCBWDataTransferLength = htole32(transfer_length);
    cbw->bmCBWFlags = flags;
    cbw->bCBWLUN = msd->lun;
    cbw->bCBWCBLength = command_len;

    // copy command_len bytes from the command passed in into the command_len
    memcpy(cbw->CBWCB, command, command_len);
    ums_queue_request(msd, txn);
    return NO_ERROR;
}

static mx_status_t ums_queue_csw(ums_t* msd) {
    list_node_t* csw_node = list_remove_head(&msd->free_csw_reqs);
    if (!csw_node) {
        DEBUG_PRINT(("UMS:error, no CSW reqs left\n"));
        return ERR_BUFFER_TOO_SMALL;
    }
    iotxn_t* csw_request = containerof(csw_node, iotxn_t, node);
    csw_request->complete_cb = ums_csw_complete;
    csw_request->cookie = msd;
    ums_queue_request(msd, csw_request);
    return NO_ERROR;
}

static void ums_read_csw_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t *)cookie);
}

static mx_status_t ums_read_csw(ums_t* msd) {
    list_node_t* csw_node = list_remove_head(&msd->free_csw_reqs);
    if (!csw_node) {
        DEBUG_PRINT(("UMS:error, no CSW reqs left\n"));
        return ERR_BUFFER_TOO_SMALL;
    }

    completion_t completion = COMPLETION_INIT;
    iotxn_t* csw_request = containerof(csw_node, iotxn_t, node);
    csw_request->complete_cb = ums_read_csw_complete;
    csw_request->cookie = &completion;
    ums_queue_request(msd, csw_request);
    completion_wait(&completion, MX_TIME_INFINITE);

    csw_status_t csw_error = ums_verify_csw(msd, csw_request);
    list_add_tail(&msd->free_csw_reqs, &csw_request->node);

    if (csw_error == CSW_SUCCESS) {
        return NO_ERROR;
    } else if (csw_error == CSW_FAILED) {
        return ERR_BAD_STATE;
    } else {
        // FIXME - best way to handle this?
        // print error and then reset device due to it
        DEBUG_PRINT(("UMS: CSW verify returned error. Check ums-hw.h csw_status_t for enum = %d\n", csw_error));
        ums_reset(msd);
        return ERR_INTERNAL;
    }
}

static mx_status_t ums_queue_read(ums_t* msd, uint16_t transfer_length) {
    // read request sense response
    list_node_t* read_node = list_remove_head(&msd->free_read_reqs);
    if (!read_node) {
        DEBUG_PRINT(("UMS:error, no read reqs left\n"));
        return ERR_BUFFER_TOO_SMALL;
    }
    iotxn_t* read_request = containerof(read_node, iotxn_t, node);
    read_request->length = transfer_length;
    ums_queue_request(msd, read_request);
    return NO_ERROR;
}

static mx_status_t ums_queue_write(ums_t* msd, uint16_t transfer_length, iotxn_t* txn) {
    list_node_t* write_node = list_remove_head(&msd->free_write_reqs);
    if (!write_node) {
        DEBUG_PRINT(("UMS:error, no write reqs left\n"));
        return ERR_BUFFER_TOO_SMALL;
    }
    iotxn_t* write_request = containerof(write_node, iotxn_t, node);
    write_request->length = transfer_length;
    void* buffer;
    iotxn_mmap(write_request, &buffer);
    iotxn_copyfrom(txn, buffer, (size_t)transfer_length, 0);
    ums_queue_request(msd, write_request);
    return NO_ERROR;
}

static csw_status_t ums_verify_csw(ums_t* msd, iotxn_t* csw_request) {
    ums_csw_t csw;
    iotxn_copyfrom(csw_request, &csw, sizeof(csw), 0);

    // check signature is "USBS"
    if (letoh32(csw.dCSWSignature) != CSW_SIGNATURE) {
        DEBUG_PRINT(("UMS:invalid csw sig: %08x \n", letoh32(csw.dCSWSignature)));
        return CSW_INVALID;
    }
    // check if tag matches the tag of last CBW
    if (letoh32(csw.dCSWTag) != msd->tag_receive++) {
        DEBUG_PRINT(("UMS:csw tag mismatch, expected:%08x got in csw:%08x \n", msd->tag_receive - 1,
                    letoh32(csw.dCSWTag)));
        return CSW_TAG_MISMATCH;
    }
    // check if success is true or not?
    if (csw.bmCSWStatus == CSW_FAILED) {
        return CSW_FAILED;
    } else if (csw.bmCSWStatus == CSW_PHASE_ERROR) {
        return CSW_PHASE_ERROR;
    }
    return CSW_SUCCESS;
}

static void ums_read_complete(iotxn_t* txn, void* cookie) {
    ums_t* msd = (ums_t*)cookie;
    list_add_tail(&msd->completed_reads, &txn->node);
}

static iotxn_t* pop_completed_read(ums_t* msd) {
    list_node_t* node = list_remove_head(&msd->completed_reads);
    if (!node) {
        printf("msd->completed_reads empty in pop_completed_read\n");
        return NULL;
    }
    return containerof(node, iotxn_t, node);
}

static void ums_csw_complete(iotxn_t* csw_request, void* cookie) {
    ums_t* msd = (ums_t*)cookie;

    // TODO: handle error case for CSW by setting iotxn to error and returning
    iotxn_t* curr_txn = list_remove_head_type(&msd->queued_iotxns, iotxn_t, node);

    // unblock calls to IOCTL_DEVICE_SYNC that are waiting for curr_txn to complete
    mtx_lock(&msd->mutex);
    ums_sync_node_t* sync_node;
    list_for_every_entry(&msd->sync_nodes, sync_node, ums_sync_node_t, node) {
        if (sync_node->iotxn == curr_txn) {
            list_delete(&sync_node->node);
            completion_signal(&sync_node->completion);
        }
    }
    mtx_unlock(&msd->mutex);

    csw_status_t csw_error = ums_verify_csw(msd, csw_request);
    if (csw_error) {
        // print error and then reset device due to it
        DEBUG_PRINT(("UMS: CSW verify returned error. Check ums-hw.h csw_status_t for enum = %d\n", csw_error));
        if (csw_error != CSW_FAILED) {
            ums_reset(msd);
        }
        list_add_tail(&msd->free_csw_reqs, &csw_request->node);
        iotxn_complete(curr_txn, ERR_BAD_STATE, 0);
        return;
    }
    // if head of iotxn list is a read iotxn and CSW reports success, then set its buffer to that
    // of the latest read request, with limited length based on data residue field in CSW
    if (curr_txn->opcode == IOTXN_OP_READ) {
        iotxn_t* read_request = pop_completed_read(msd);
        if (!read_request) {
            list_add_tail(&msd->free_csw_reqs, &csw_request->node);
            return;
        }
        // data residue field is the 3rd uint32_t in csw buffer
        uint32_t temp;
        iotxn_copyfrom(csw_request, &temp, sizeof(temp), 2 * sizeof(temp));
        uint32_t residue = letoh32(temp);
        uint32_t length = read_request->actual - residue;
        void* buffer;
        iotxn_mmap(curr_txn, &buffer);
        iotxn_copyfrom(read_request, buffer, length, 0);
        list_add_tail(&msd->free_read_reqs, &read_request->node);
    }

    list_add_tail(&msd->free_csw_reqs, &csw_request->node);
    iotxn_complete(curr_txn, NO_ERROR, curr_txn->length);
}

static void ums_write_complete(iotxn_t* txn, void* cookie) {
    ums_t* msd = (ums_t*)cookie;
    // FIXME what to do with error here?
    list_add_tail(&msd->free_write_reqs, &txn->node);
}

static mx_status_t ums_inquiry(ums_t* msd, uint8_t* out_data) {
    mx_status_t status = NO_ERROR;

    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_INQUIRY;
    command.length = UMS_INQUIRY_TRANSFER_LENGTH;
    status = ums_send_cbw(msd, UMS_INQUIRY_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // read inquiry response
    status = ums_queue_read(msd, UMS_INQUIRY_TRANSFER_LENGTH);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // wait for CSW
    status = ums_read_csw(msd);
    iotxn_t* read_request = pop_completed_read(msd);
    if (status == NO_ERROR) {
        iotxn_copyfrom(read_request, out_data, UMS_INQUIRY_TRANSFER_LENGTH, 0);
    }
    list_add_tail(&msd->free_read_reqs, &read_request->node);
    return status;
}

static mx_status_t ums_test_unit_ready(ums_t* msd) {
    mx_status_t status = NO_ERROR;

    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_TEST_UNIT_READY;
    status = ums_send_cbw(msd, 0, USB_DIR_IN, sizeof(command), &command);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // wait for CSW
    return ums_read_csw(msd);
}

static mx_status_t ums_request_sense(ums_t* msd, uint8_t* out_data) {
    mx_status_t status = NO_ERROR;

    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_REQUEST_SENSE;
    command.length = UMS_REQUEST_SENSE_TRANSFER_LENGTH;
    status = ums_send_cbw(msd, UMS_REQUEST_SENSE_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // read request sense response
    status = ums_queue_read(msd, UMS_REQUEST_SENSE_TRANSFER_LENGTH);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // wait for CSW
    status = ums_read_csw(msd);
    iotxn_t* read_request = pop_completed_read(msd);
    if (status == NO_ERROR) {
        iotxn_copyfrom(read_request, out_data, UMS_REQUEST_SENSE_TRANSFER_LENGTH, 0);
    }
    list_add_tail(&msd->free_read_reqs, &read_request->node);
    return status;
}

static mx_status_t ums_read_format_capacities(ums_t* msd, uint8_t* out_data) {
    mx_status_t status = NO_ERROR;

    // CBW Configuration
    scsi_command10_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_READ_FORMAT_CAPACITIES;
    command.length_lo = UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH;
    status = ums_send_cbw(msd, UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // read request sense response
    status = ums_queue_read(msd, UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // wait for CSW
    status = ums_read_csw(msd);
    iotxn_t* read_request = pop_completed_read(msd);
    if (status == NO_ERROR) {
        iotxn_copyfrom(read_request, out_data, UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH, 0);
    }
    list_add_tail(&msd->free_read_reqs, &read_request->node);
    return status;
}

static mx_status_t ums_read_capacity10(ums_t* msd, scsi_read_capacity_10_t* out_data) {
    mx_status_t status = NO_ERROR;

    // CBW Configuration
    scsi_command10_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_READ_CAPACITY10;
    status = ums_send_cbw(msd, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // read capacity10 response
    status = ums_queue_read(msd, sizeof(*out_data));
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    status = ums_read_csw(msd);
    iotxn_t* read_request = pop_completed_read(msd);
    if (status == NO_ERROR) {
        iotxn_copyfrom(read_request, out_data, sizeof(*out_data), 0);
    }
    list_add_tail(&msd->free_read_reqs, &read_request->node);
    return status;
}

static mx_status_t ums_read_capacity16(ums_t* msd, scsi_read_capacity_16_t* out_data) {
    mx_status_t status = NO_ERROR;

    // CBW Configuration
    scsi_command16_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_READ_CAPACITY16;
    // service action = 10, not sure what that means
    command.misc = 0x10;
    command.length = sizeof(*out_data);
    status = ums_send_cbw(msd, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // read capacity16 response
    status = ums_queue_read(msd, sizeof(*out_data));
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    status = ums_read_csw(msd);
    iotxn_t* read_request = pop_completed_read(msd);
    if (status == NO_ERROR) {
        iotxn_copyfrom(read_request, out_data, sizeof(*out_data), 0);
    }
    list_add_tail(&msd->free_read_reqs, &read_request->node);
    return status;
}

static mx_status_t ums_read(mx_device_t* device, iotxn_t* txn) {
    if (txn->length > UINT32_MAX) return ERR_INVALID_ARGS;
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;

    uint64_t lba = txn->offset/msd->block_size;
    uint32_t num_blocks = txn->length/msd->block_size;
    uint32_t transfer_length = txn->length;

    // CBW Configuration

    if (msd->use_read_write_16) {
        scsi_command16_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_READ16;
        command.lba = htobe64(lba);
        command.length = htobe32(num_blocks);

        status = ums_send_cbw(msd, transfer_length, USB_DIR_IN, sizeof(command), &command);
        if (status == ERR_BUFFER_TOO_SMALL) {
            return status;
        }
    } else if (num_blocks <= UINT16_MAX) {
        scsi_command10_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_READ10;
        command.lba = htobe32(lba);
        command.length_hi = num_blocks >> 8;
        command.length_lo = num_blocks & 0xFF;
        status = ums_send_cbw(msd, transfer_length, USB_DIR_IN, sizeof(command), &command);
        if (status == ERR_BUFFER_TOO_SMALL) {
            return status;
        }
    } else {
        scsi_command12_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_READ12;
        command.lba = htobe32(lba);
        command.length = htobe32(num_blocks);
        status = ums_send_cbw(msd, transfer_length, USB_DIR_IN, sizeof(command), &command);
        if (status == ERR_BUFFER_TOO_SMALL) {
            return status;
        }
    }

    // read read response
    status = ums_queue_read(msd, transfer_length);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // receive CSW
    return ums_queue_csw(msd);
}

static mx_status_t ums_write(mx_device_t* device, iotxn_t* txn) {
    if (txn->length > UINT32_MAX) return ERR_INVALID_ARGS;
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;

    uint64_t lba = txn->offset/msd->block_size;
    uint32_t num_blocks = txn->length/msd->block_size;
    uint32_t transfer_length = txn->length;

    if (msd->use_read_write_16) {
        scsi_command16_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE16;
        command.lba = htobe64(lba);
        command.length = htobe32(num_blocks);
        status = ums_send_cbw(msd, transfer_length, USB_DIR_OUT, sizeof(command), &command);
        if (status == ERR_BUFFER_TOO_SMALL) {
            return status;
        }
    } else if (num_blocks <= UINT16_MAX) {
        scsi_command10_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE10;
        command.lba = htobe32(lba);
        command.length_hi = num_blocks >> 8;
        command.length_lo = num_blocks & 0xFF;
        status = ums_send_cbw(msd, transfer_length, USB_DIR_OUT, sizeof(command), &command);
        if (status == ERR_BUFFER_TOO_SMALL) {
            return status;
        }
    } else {
        scsi_command12_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE12;
        command.lba = htobe32(lba);
        command.length = htobe32(num_blocks);
        status = ums_send_cbw(msd, transfer_length, USB_DIR_OUT, sizeof(command), &command);
        if (status == ERR_BUFFER_TOO_SMALL) {
            return status;
        }
    }

    //write response
    status = ums_queue_write(msd, transfer_length, txn);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // receive CSW
    return ums_queue_csw(msd);
}

static mx_status_t ums_toggle_removable(mx_device_t* device, bool removable) {
    ums_t* msd = get_ums(device);
    mx_status_t status = NO_ERROR;

    // CBW Configuration
    scsi_command12_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_TOGGLE_REMOVABLE;
    status = ums_send_cbw(msd, 0, USB_DIR_OUT, sizeof(command), &command);
    if (status == ERR_BUFFER_TOO_SMALL) {
        return status;
    }

    // receive CSW
    return ums_read_csw(msd);
}

static void ums_unbind(mx_device_t* device) {
    ums_t* msd = get_ums(device);
    device_remove(&msd->device);
}

static mx_status_t ums_release(mx_device_t* device) {
    ums_t* msd = get_ums(device);
    iotxn_t* txn;
    while ((txn = list_remove_head_type(&msd->free_csw_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&msd->free_read_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&msd->free_write_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }

    free(msd);
    return NO_ERROR;
}

static void ums_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    ums_t* msd = get_ums(dev);
    mtx_lock(&msd->mutex);

    uint32_t block_size = msd->block_size;
    // offset must be aligned to block size
    if (txn->offset % block_size) {
        DEBUG_PRINT(("UMS:offset on iotxn (%" PRIu64 ") not aligned to block size(%d)\n", txn->offset, block_size));
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        goto out;
    }

    if (txn->length % block_size) {
        DEBUG_PRINT(("UMS:length on iotxn (%" PRIu64 ") not aligned to block size(%d)\n", txn->length, block_size));
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        goto out;
    }

    mx_status_t status;
    if (txn->opcode == IOTXN_OP_READ) {
        status = ums_read(dev, txn);
    }else if (txn->opcode == IOTXN_OP_WRITE) {
        status = ums_write(dev, txn);
    } else {
        status = ERR_INVALID_ARGS;
    }
    if (status == NO_ERROR) {
        list_add_tail(&msd->queued_iotxns, &txn->node);
    } else {
        iotxn_complete(txn, status, 0);
    }

out:
    mtx_unlock(&msd->mutex);
}

static ssize_t ums_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    ums_t* msd = get_ums(dev);
    // TODO implement other block ioctls
    switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size)) return ERR_BUFFER_TOO_SMALL;
        *size = msd->total_blocks * msd->block_size;
        return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
         uint64_t* blksize = reply;
         if (max < sizeof(*blksize)) return ERR_BUFFER_TOO_SMALL;
         *blksize = msd->block_size;
         return sizeof(*blksize);
    }
    case IOCTL_DEVICE_SYNC: {
        ums_sync_node_t node;

        mtx_lock(&msd->mutex);
        if (list_is_empty(&msd->queued_iotxns)) {
            mtx_unlock(&msd->mutex);
            return NO_ERROR;
        }
        // queue a stack allocated sync node on ums_t.sync_nodes
        node.iotxn = list_peek_tail_type(&msd->queued_iotxns, iotxn_t, node);
        completion_reset(&node.completion);
        list_add_head(&msd->sync_nodes, &node.node);
        mtx_unlock(&msd->mutex);

        return completion_wait(&node.completion, MX_TIME_INFINITE);
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
    .ioctl = ums_ioctl,
    .unbind = ums_unbind,
    .release = ums_release,
    .iotxn_queue = ums_iotxn_queue,
    .get_size = ums_get_size,
};

static int ums_start_thread(void* arg) {
    ums_t* msd = (ums_t*)arg;
    device_init(&msd->device, msd->driver, "usb_mass_storage", &ums_device_proto);

    // we need to send the Inquiry command first,
    // but currently we do not do anything with the response
    uint8_t inquiry_data[UMS_INQUIRY_TRANSFER_LENGTH];
    mx_status_t status = ums_inquiry(msd, inquiry_data);
    if (status < 0) {
        printf("ums_inquiry failed: %d\n", status);
        goto fail;
    }

    bool ready = false;
    for (int i = 0; i < 100; i++) {
        status = ums_test_unit_ready(msd);
        if (status == NO_ERROR) {
            ready = true;
            break;
        } else if (status != ERR_BAD_STATE) {
            printf("ums_test_unit_ready failed: %d\n", status);
            goto fail;
        } else {
            uint8_t request_sense_data[UMS_REQUEST_SENSE_TRANSFER_LENGTH];
            status = ums_request_sense(msd, request_sense_data);
            if (status != NO_ERROR) {
                printf("request_sense_data failed: %d\n", status);
                goto fail;
            }
            // wait a bit before trying ums_test_unit_ready again
            usleep(100 * 1000);
        }
    }
    if (!ready) {
        printf("gave up waiting for ums_test_unit_ready to succeed\n");
        goto fail;
    }

    scsi_read_capacity_10_t data;
    status = ums_read_capacity10(msd, &data);
    if (status < 0) {
        printf("read_capacity10 failed: %d\n", status);
        goto fail;
    }

    // +1 because this returns the address of the final block, and blocks are zero indexed
    msd->total_blocks = betoh32(data.lba) + 1;
    msd->block_size = betoh32(data.block_length);

    if (msd->total_blocks == 0xFFFFFFFF) {
        scsi_read_capacity_16_t data;
        status = ums_read_capacity16(msd, &data);
        if (status < 0) {
            printf("read_capacity16 failed: %d\n", status);
            goto fail;
        }

        // +1 because this returns the address of the final block, and blocks are zero indexed
        msd->total_blocks = betoh64(data.lba) + 1;
        msd->block_size = betoh64(data.block_length);
    }

    // Need to use READ16/WRITE16 if block addresses are greater than 32 bit
    msd->use_read_write_16 = msd->total_blocks > UINT32_MAX;

    DEBUG_PRINT(("UMS:block size is: 0x%08x\n", msd->block_size));
    DEBUG_PRINT(("UMS:total blocks is: %" PRId64 "\n", msd->total_blocks));
    DEBUG_PRINT(("UMS:total size is: %" PRId64 "\n", msd->total_blocks * msd->block_size));
    msd->device.protocol_id = MX_PROTOCOL_BLOCK;
    status = device_add(&msd->device, msd->udev);
    if (status == NO_ERROR) return NO_ERROR;

fail:
    printf("ums_start_thread failed\n");
    ums_release(&msd->device);
    return status;
}

static mx_status_t ums_bind(mx_driver_t* driver, mx_device_t* device, void** cookie) {
    // find our endpoints
    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(device, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf) {
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
    }
    if (intf->bNumEndpoints < 2) {
        DEBUG_PRINT(("UMS:ums_bind wrong number of endpoints: %d\n", intf->bNumEndpoints));
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
    }

    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;

   usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    while (endp) {
        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_out_addr = endp->bEndpointAddress;
            }
        } else {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_in_addr = endp->bEndpointAddress;
            }
        }
        endp = usb_desc_iter_next_endpoint(&iter);
    }
    usb_desc_iter_release(&iter);

    if (!bulk_in_addr || !bulk_out_addr) {
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
    list_initialize(&msd->queued_iotxns);
    list_initialize(&msd->completed_reads);
    list_initialize(&msd->completed_csws);
    list_initialize(&msd->sync_nodes);

    msd->udev = device;
    msd->driver = driver;
    msd->bulk_in_addr = bulk_in_addr;
    msd->bulk_out_addr = bulk_out_addr;

    mx_status_t status = NO_ERROR;
    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(bulk_in_addr, USB_BUF_SIZE);
        if (!txn) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        txn->complete_cb = ums_read_complete;
        txn->cookie = msd;
        list_add_head(&msd->free_read_reqs, &txn->node);
    }
    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(bulk_in_addr, sizeof(ums_csw_t));
        if (!txn) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        txn->length = sizeof(ums_csw_t);
        // complete_cb and cookie are set later
        list_add_head(&msd->free_csw_reqs, &txn->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(bulk_out_addr, USB_BUF_SIZE);
        if (!txn) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        txn->complete_cb = ums_write_complete;
        txn->cookie = msd;
        list_add_head(&msd->free_write_reqs, &txn->node);
    }

    uint8_t lun = 0;
    ums_get_max_lun(msd, (void*)&lun);
    DEBUG_PRINT(("UMS:Max lun is: %02x\n", (unsigned char)lun));
    msd->tag_send = msd->tag_receive = 8;
    // TODO: get this lun from some sort of valid way. not sure how multilun support works
    msd->lun = 0;
    thrd_t thread;
    thrd_create_with_name(&thread, ums_start_thread, msd, "ums_start_thread");
    thrd_detach(thread);

    return NO_ERROR;

fail:
    printf("ums_bind failed: %d\n", status);
    ums_release(&msd->device);
    return status;
}

mx_driver_t _driver_usb_mass_storage = {
    .ops = {
        .bind = ums_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_usb_mass_storage, "usb-mass-storage", "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_MSC),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, 6),      // SCSI transparent command set
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0x50),   // bulk-only protocol
MAGENTA_DRIVER_END(_driver_usb_mass_storage)
