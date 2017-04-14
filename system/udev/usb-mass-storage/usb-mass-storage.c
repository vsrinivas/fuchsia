// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <magenta/assert.h>
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
    size_t max_transfer;  // maximum transfer size reported by usb_get_max_transfer_size()

    bool use_read_write_16; // use READ16 and WRITE16 if total_blocks > 0xFFFFFFFF

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;
    size_t bulk_in_max_packet;
    size_t bulk_out_max_packet;

    // FIXME (voydanoff) We only need small buffers for CBW, CSW and SCSI commands
    // Once we have the new iotxn API we can allocate these iotxns inline
    // and use a single VMO for their buffers
    iotxn_t* cbw_iotxn;
    iotxn_t* data_iotxn;
    iotxn_t* csw_iotxn;

    thrd_t worker_thread;
    bool dead;

    // list of queued io transactions
    list_node_t queued_iotxns;
    // used to signal ums_worker_thread when new iotxns are ready for processing
    // and when device is dead
    completion_t iotxn_completion;
    // protects queued_iotxns, iotxn_completion and dead
    mtx_t iotxn_lock;

    // list of active ums_sync_node_t
    list_node_t sync_nodes;
    // current iotxn being processed (needed for IOCTL_DEVICE_SYNC)
    iotxn_t* curr_txn;
} ums_t;
#define get_ums(dev) containerof(dev, ums_t, device)

// extra data for clone txns
typedef struct {
    ums_t*      msd;
    mx_off_t    offset;
    size_t      total_length;
    size_t      max_packet;
} ums_txn_extra_t;
static_assert(sizeof(ums_txn_extra_t) <= sizeof (iotxn_extra_data_t), "");

static csw_status_t ums_verify_csw(ums_t* msd, iotxn_t* csw_request, uint32_t* out_residue);

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

static void ums_queue_request(ums_t* msd, iotxn_t* txn) {
    iotxn_queue(msd->udev, txn);
}

static void ums_txn_complete(iotxn_t* txn, void* cookie) {
    if (cookie) {
        completion_signal((completion_t *)cookie);
    }
}

static void ums_send_cbw(ums_t* msd, uint32_t transfer_length, uint8_t flags,
                                uint8_t command_len, void* command) {
    iotxn_t* txn = msd->cbw_iotxn;

    ums_cbw_t* cbw;
    iotxn_mmap(txn, (void **)&cbw);

    memset(cbw, 0, sizeof(*cbw));
    cbw->dCBWSignature = htole32(CBW_SIGNATURE);
    cbw->dCBWTag = htole32(msd->tag_send++);
    cbw->dCBWDataTransferLength = htole32(transfer_length);
    cbw->bmCBWFlags = flags;
    cbw->bCBWLUN = msd->lun;
    cbw->bCBWCBLength = command_len;

    // copy command_len bytes from the command passed in into the command_len
    memcpy(cbw->CBWCB, command, command_len);
    txn->cookie = NULL;
    ums_queue_request(msd, txn);
}

static mx_status_t ums_read_csw(ums_t* msd, uint32_t* out_residue) {
    completion_t completion = COMPLETION_INIT;
    iotxn_t* csw_request = msd->csw_iotxn;
    csw_request->cookie = &completion;
    ums_queue_request(msd, csw_request);
    completion_wait(&completion, MX_TIME_INFINITE);

    csw_status_t csw_error = ums_verify_csw(msd, csw_request, out_residue);

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

static csw_status_t ums_verify_csw(ums_t* msd, iotxn_t* csw_request, uint32_t* out_residue) {
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

    if (out_residue) {
        *out_residue = letoh32(csw.dCSWDataResidue);
    }
    return CSW_SUCCESS;
}

static void ums_queue_read(ums_t* msd, uint16_t transfer_length) {
    // read request sense response
    iotxn_t* read_request = msd->data_iotxn;
    read_request->length = transfer_length;
    read_request->cookie = NULL;
    ums_queue_request(msd, read_request);
}

static mx_status_t ums_inquiry(ums_t* msd, uint8_t* out_data) {
    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_INQUIRY;
    command.length = UMS_INQUIRY_TRANSFER_LENGTH;
    ums_send_cbw(msd, UMS_INQUIRY_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);

    // read inquiry response
    ums_queue_read(msd, UMS_INQUIRY_TRANSFER_LENGTH);

    // wait for CSW
    mx_status_t status = ums_read_csw(msd, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(msd->data_iotxn, out_data, UMS_INQUIRY_TRANSFER_LENGTH, 0);
    }
    return status;
}

static mx_status_t ums_test_unit_ready(ums_t* msd) {
    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_TEST_UNIT_READY;
    ums_send_cbw(msd, 0, USB_DIR_IN, sizeof(command), &command);

    // wait for CSW
    return ums_read_csw(msd, NULL);
}

static mx_status_t ums_request_sense(ums_t* msd, uint8_t* out_data) {
    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_REQUEST_SENSE;
    command.length = UMS_REQUEST_SENSE_TRANSFER_LENGTH;
    ums_send_cbw(msd, UMS_REQUEST_SENSE_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);

    // read request sense response
    ums_queue_read(msd, UMS_REQUEST_SENSE_TRANSFER_LENGTH);

    // wait for CSW
    mx_status_t status = ums_read_csw(msd, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(msd->data_iotxn, out_data, UMS_REQUEST_SENSE_TRANSFER_LENGTH, 0);
    }
    return status;
}

static mx_status_t ums_read_format_capacities(ums_t* msd, uint8_t* out_data) {
    // CBW Configuration
    scsi_command10_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_READ_FORMAT_CAPACITIES;
    command.length_lo = UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH;
    ums_send_cbw(msd, UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);

    // read request sense response
    ums_queue_read(msd, UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH);

    // wait for CSW
    mx_status_t status = ums_read_csw(msd, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(msd->data_iotxn, out_data, UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH, 0);
    }
    return status;
}

static mx_status_t ums_read_capacity10(ums_t* msd, scsi_read_capacity_10_t* out_data) {
    // CBW Configuration
    scsi_command10_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_READ_CAPACITY10;
    ums_send_cbw(msd, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);

    // read capacity10 response
    ums_queue_read(msd, sizeof(*out_data));

    mx_status_t status = ums_read_csw(msd, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(msd->data_iotxn, out_data, sizeof(*out_data), 0);
    }
    return status;
}

static mx_status_t ums_read_capacity16(ums_t* msd, scsi_read_capacity_16_t* out_data) {
    // CBW Configuration
    scsi_command16_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_READ_CAPACITY16;
    // service action = 10, not sure what that means
    command.misc = 0x10;
    command.length = sizeof(*out_data);
    ums_send_cbw(msd, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);

    // read capacity16 response
    ums_queue_read(msd, sizeof(*out_data));

    mx_status_t status = ums_read_csw(msd, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(msd->data_iotxn, out_data, sizeof(*out_data), 0);
    }
    return status;
}

static void clone_complete(iotxn_t* clone, void* cookie) {
    ums_txn_extra_t* extra = (ums_txn_extra_t *)&clone->extra;
    ums_t* msd = extra->msd;

    if (clone->status == NO_ERROR) {
        extra->offset += clone->actual;
        // Queue another read if we haven't read full length and did not receive a short packet
        if (extra->offset < extra->total_length && clone->actual != 0 &&
                (clone->actual % extra->max_packet) == 0) {
            size_t length = extra->total_length - extra->offset;
            if (length > msd->max_transfer) {
                length = msd->max_transfer;
            }
            clone->length = length;
            clone->vmo_offset += clone->actual;
            ums_queue_request(msd, clone);
            return;
        }
    }

    // transfer is done if we get here
    completion_signal((completion_t *)cookie);
}

static void ums_queue_data_transfer(ums_t* msd, iotxn_t* txn, uint8_t ep_address, size_t max_packet) {
    iotxn_t* clone = NULL;
    mx_status_t status = iotxn_clone(txn, &clone);
    if (status != NO_ERROR) {
        iotxn_complete(txn, status, 0);
        return;
    }

    // stash msd in txn->context so we can get at it in clone_complete()
    txn->context = msd;
    clone->complete_cb = clone_complete;

    ums_txn_extra_t* extra = (ums_txn_extra_t *)&clone->extra;
    extra->msd = msd;
    extra->offset = 0;
    extra->total_length = txn->length;
    extra->max_packet = max_packet;

    if (clone->length > msd->max_transfer) {
        clone->length = msd->max_transfer;
    }

    usb_protocol_data_t* pdata = iotxn_pdata(clone, usb_protocol_data_t);
    memset(pdata, 0, sizeof(*pdata));
    pdata->ep_address = ep_address;

    completion_t completion = COMPLETION_INIT;
    clone->cookie = &completion;
    ums_queue_request(msd, clone);
    completion_wait(&completion, MX_TIME_INFINITE);

    txn->status = clone->status;
    txn->actual = (txn->status == NO_ERROR ? extra->offset : 0);

    iotxn_release(clone);
}

static mx_status_t ums_read(ums_t* msd, iotxn_t* txn) {
    if (txn->length > UINT32_MAX) return ERR_INVALID_ARGS;

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

        ums_send_cbw(msd, transfer_length, USB_DIR_IN, sizeof(command), &command);
    } else if (num_blocks <= UINT16_MAX) {
        scsi_command10_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_READ10;
        command.lba = htobe32(lba);
        command.length_hi = num_blocks >> 8;
        command.length_lo = num_blocks & 0xFF;
        ums_send_cbw(msd, transfer_length, USB_DIR_IN, sizeof(command), &command);
    } else {
        scsi_command12_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_READ12;
        command.lba = htobe32(lba);
        command.length = htobe32(num_blocks);
        ums_send_cbw(msd, transfer_length, USB_DIR_IN, sizeof(command), &command);
    }

    ums_queue_data_transfer(msd, txn, msd->bulk_in_addr, msd->bulk_in_max_packet);

    uint32_t residue;
    mx_status_t status = ums_read_csw(msd, &residue);
    if (status == NO_ERROR) {
        status = txn->actual - residue;
    }

    return status;
}

static mx_status_t ums_write(ums_t* msd, iotxn_t* txn) {
    if (txn->length > UINT32_MAX) return ERR_INVALID_ARGS;

    uint64_t lba = txn->offset/msd->block_size;
    uint32_t num_blocks = txn->length/msd->block_size;
    uint32_t transfer_length = txn->length;

    if (msd->use_read_write_16) {
        scsi_command16_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE16;
        command.lba = htobe64(lba);
        command.length = htobe32(num_blocks);
        ums_send_cbw(msd, transfer_length, USB_DIR_OUT, sizeof(command), &command);
    } else if (num_blocks <= UINT16_MAX) {
        scsi_command10_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE10;
        command.lba = htobe32(lba);
        command.length_hi = num_blocks >> 8;
        command.length_lo = num_blocks & 0xFF;
        ums_send_cbw(msd, transfer_length, USB_DIR_OUT, sizeof(command), &command);
    } else {
        scsi_command12_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE12;
        command.lba = htobe32(lba);
        command.length = htobe32(num_blocks);
        ums_send_cbw(msd, transfer_length, USB_DIR_OUT, sizeof(command), &command);
    }

    ums_queue_data_transfer(msd, txn, msd->bulk_out_addr, msd->bulk_out_max_packet);

    // receive CSW
    uint32_t residue;
    mx_status_t status = ums_read_csw(msd, &residue);
    if (status == NO_ERROR) {
        status = transfer_length - residue;
    }

    return status;
}

static mx_status_t ums_toggle_removable(mx_device_t* device, bool removable) {
    ums_t* msd = get_ums(device);

    // CBW Configuration
    scsi_command12_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_TOGGLE_REMOVABLE;
    ums_send_cbw(msd, 0, USB_DIR_OUT, sizeof(command), &command);

    // receive CSW
    return ums_read_csw(msd, NULL);
}

static void ums_unbind(mx_device_t* device) {
    ums_t* msd = get_ums(device);
    device_remove(&msd->device);
}

static mx_status_t ums_release(mx_device_t* device) {
    ums_t* msd = get_ums(device);

    // terminate our worker thread
    mtx_lock(&msd->iotxn_lock);
    msd->dead = true;
    mtx_unlock(&msd->iotxn_lock);
    completion_signal(&msd->iotxn_completion);
    thrd_join(msd->worker_thread, NULL);

    if (msd->cbw_iotxn) {
        iotxn_release(msd->cbw_iotxn);
    }
    if (msd->data_iotxn) {
        iotxn_release(msd->data_iotxn);
    }
    if (msd->csw_iotxn) {
        iotxn_release(msd->csw_iotxn);
    }

    free(msd);
    return NO_ERROR;
}

static void ums_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    ums_t* msd = get_ums(dev);

    uint32_t block_size = msd->block_size;
    // offset must be aligned to block size
    if (txn->offset % block_size) {
        DEBUG_PRINT(("UMS:offset on iotxn (%" PRIu64 ") not aligned to block size(%d)\n",
                    txn->offset, block_size));
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }
    if (txn->length % block_size) {
        DEBUG_PRINT(("UMS:length on iotxn (%" PRIu64 ") not aligned to block size(%d)\n",
                    txn->length, block_size));
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    mtx_lock(&msd->iotxn_lock);
    list_add_tail(&msd->queued_iotxns, &txn->node);
    mtx_unlock(&msd->iotxn_lock);
    completion_signal(&msd->iotxn_completion);
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

        mtx_lock(&msd->iotxn_lock);
        iotxn_t* txn = list_peek_tail_type(&msd->queued_iotxns, iotxn_t, node);
        if (!txn) {
            txn = msd->curr_txn;
        }
        if (!txn) {
            mtx_unlock(&msd->iotxn_lock);
            return NO_ERROR;
        }
        // queue a stack allocated sync node on ums_t.sync_nodes
        node.iotxn = txn;
        completion_reset(&node.completion);
        list_add_head(&msd->sync_nodes, &node.node);
        mtx_unlock(&msd->iotxn_lock);

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

static int ums_worker_thread(void* arg) {
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

    msd->total_blocks = betoh32(data.lba);
    msd->block_size = betoh32(data.block_length);

    if (msd->total_blocks == 0xFFFFFFFF) {
        scsi_read_capacity_16_t data;
        status = ums_read_capacity16(msd, &data);
        if (status < 0) {
            printf("read_capacity16 failed: %d\n", status);
            goto fail;
        }

        msd->total_blocks = betoh64(data.lba);
        msd->block_size = betoh32(data.block_length);
    }
    if (msd->block_size == 0) {
        printf("UMS zero block size\n");
        status = ERR_INVALID_ARGS;
        goto fail;
    }

    // +1 because this returns the address of the final block, and blocks are zero indexed
    msd->total_blocks++;

    // Need to use READ16/WRITE16 if block addresses are greater than 32 bit
    msd->use_read_write_16 = msd->total_blocks > UINT32_MAX;

    DEBUG_PRINT(("UMS:block size is: 0x%08x\n", msd->block_size));
    DEBUG_PRINT(("UMS:total blocks is: %" PRId64 "\n", msd->total_blocks));
    DEBUG_PRINT(("UMS:total size is: %" PRId64 "\n", msd->total_blocks * msd->block_size));
    msd->device.protocol_id = MX_PROTOCOL_BLOCK;
    status = device_add(&msd->device, msd->udev);
    if (status != NO_ERROR) {
        printf("ums device_add failed: %d\n", status);
        goto fail;
    }

    while (1) {
        completion_wait(&msd->iotxn_completion, MX_TIME_INFINITE);

        mtx_lock(&msd->iotxn_lock);
        if (msd->dead) {
            mtx_unlock(&msd->iotxn_lock);
            return 0;
        }
        completion_reset(&msd->iotxn_completion);
        iotxn_t* txn = list_remove_head_type(&msd->queued_iotxns, iotxn_t, node);
        MX_DEBUG_ASSERT(txn != NULL);
        msd->curr_txn = txn;
        mtx_unlock(&msd->iotxn_lock);

        mx_status_t status;
        if (txn->opcode == IOTXN_OP_READ) {
            status = ums_read(msd, txn);
        }else if (txn->opcode == IOTXN_OP_WRITE) {
            status = ums_write(msd, txn);
        } else {
            status = ERR_INVALID_ARGS;
        }

        mtx_lock(&msd->iotxn_lock);
        // unblock calls to IOCTL_DEVICE_SYNC that are waiting for curr_txn to complete
        ums_sync_node_t* sync_node;
        ums_sync_node_t* temp;
        list_for_every_entry_safe(&msd->sync_nodes, sync_node, temp, ums_sync_node_t, node) {
            if (sync_node->iotxn == txn) {
                list_delete(&sync_node->node);
                completion_signal(&sync_node->completion);
            }
        }
        msd->curr_txn = NULL;
        mtx_unlock(&msd->iotxn_lock);

        if (status >= 0) {
            iotxn_complete(txn, NO_ERROR, status);
        } else {
            iotxn_complete(txn, status, 0);
        }
    }

fail:
    printf("ums initialization failed\n");
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
    size_t bulk_in_max_packet = 0;
    size_t bulk_out_max_packet = 0;

   usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    while (endp) {
        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_out_addr = endp->bEndpointAddress;
                bulk_out_max_packet = usb_ep_max_packet(endp);
            }
        } else {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_in_addr = endp->bEndpointAddress;
                bulk_in_max_packet = usb_ep_max_packet(endp);
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

    list_initialize(&msd->queued_iotxns);
    list_initialize(&msd->sync_nodes);
    completion_reset(&msd->iotxn_completion);
    mtx_init(&msd->iotxn_lock, mtx_plain);

    msd->udev = device;
    msd->driver = driver;
    msd->bulk_in_addr = bulk_in_addr;
    msd->bulk_out_addr = bulk_out_addr;
    msd->bulk_in_max_packet = bulk_in_max_packet;
    msd->bulk_out_max_packet = bulk_out_max_packet;

    size_t max_in = usb_get_max_transfer_size(device, bulk_in_addr);
    size_t max_out = usb_get_max_transfer_size(device, bulk_out_addr);
    msd->max_transfer = (max_in < max_out ? max_in : max_out);

    mx_status_t status = NO_ERROR;
    msd->cbw_iotxn = usb_alloc_iotxn(bulk_out_addr, sizeof(ums_cbw_t));
    if (!msd->cbw_iotxn) {
        status = ERR_NO_MEMORY;
        goto fail;
    }
    msd->data_iotxn = usb_alloc_iotxn(bulk_in_addr, PAGE_SIZE);
    if (!msd->data_iotxn) {
        status = ERR_NO_MEMORY;
        goto fail;
    }
    msd->csw_iotxn = usb_alloc_iotxn(bulk_in_addr, sizeof(ums_csw_t));
    if (!msd->csw_iotxn) {
        status = ERR_NO_MEMORY;
        goto fail;
    }

    msd->cbw_iotxn->length = sizeof(ums_cbw_t);
    msd->csw_iotxn->length = sizeof(ums_csw_t);
    msd->cbw_iotxn->complete_cb = ums_txn_complete;
    msd->data_iotxn->complete_cb = ums_txn_complete;
    msd->csw_iotxn->complete_cb = ums_txn_complete;

    uint8_t lun = 0;
    ums_get_max_lun(msd, (void*)&lun);
    DEBUG_PRINT(("UMS:Max lun is: %02x\n", (unsigned char)lun));
    msd->tag_send = msd->tag_receive = 8;
    // TODO: get this lun from some sort of valid way. not sure how multilun support works
    msd->lun = 0;

    thrd_create_with_name(&msd->worker_thread, ums_worker_thread, msd, "ums_worker_thread");

    return status;

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
