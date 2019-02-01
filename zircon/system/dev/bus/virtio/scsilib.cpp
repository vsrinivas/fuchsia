// Copyriht 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scsilib.h"

#include <ddk/debug.h>
#include <ddk/protocol/block.h>
#include <fbl/alloc_checker.h>
#include <netinet/in.h>

namespace scsi {

uint32_t CountLuns(Controller* controller, uint8_t target) {
    ReportLunsParameterDataHeader data = {};

    ReportLunsCDB cdb = {};
    cdb.opcode = Opcode::REPORT_LUNS;
    cdb.allocation_length = htonl(sizeof(data));

    auto status = controller->ExecuteCommandSync(/*target=*/target, /*lun=*/0,
        /*cdb=*/{&cdb, sizeof(cdb)},
        /*data_out=*/{nullptr, 0},
        /*data_in=*/{&data, sizeof(data)});
    if (status != ZX_OK) {
        // For now, assume REPORT LUNS is supported. A failure indicates no LUNs on this target.
        return 0;
    }
    // data.lun_list_length is 8 + the number of bytes of LUN structures.
    return (ntohl(data.lun_list_length) - 8) / 8;
}

zx_status_t Disk::Create(Controller* controller, zx_device_t* parent, uint8_t target,
                         uint16_t lun) {
    fbl::AllocChecker ac;
    auto* const disk = new (&ac) scsi::Disk(controller, parent, /*target=*/target, /*lun=*/lun);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto status = disk->Bind();
    if (status != ZX_OK) {
        delete disk;
    }
    return status;
}

zx_status_t Disk::Bind() {
    InquiryCDB inquiry_cdb = {};
    InquiryData inquiry_data = {};
    inquiry_cdb.opcode = Opcode::INQUIRY;
    inquiry_cdb.allocation_length = ntohs(sizeof(inquiry_data));

    auto status = controller_->ExecuteCommandSync(/*target=*/target_, /*lun=*/lun_,
        /*cdb=*/{&inquiry_cdb, sizeof(inquiry_cdb)},
        /*data_out=*/{nullptr, 0},
        /*data_in=*/{&inquiry_data, sizeof(inquiry_data)});
    if (status != ZX_OK) {
        return status;
    }
    // Check that its a disk first.
    if (inquiry_data.peripheral_device_type != 0) {
        return ZX_ERR_IO;
    }

    // Print T10 Vendor ID/Product ID
    zxlogf(INFO, "%d:%d ", target_, lun_);
    for (int i = 0; i < 8; i++) {
        zxlogf(INFO, "%c", inquiry_data.t10_vendor_id[i]);
    }
    zxlogf(INFO, " ");
    for (int i = 0; i < 16; i++) {
        zxlogf(INFO, "%c", inquiry_data.product_id[i]);
    }
    zxlogf(INFO, "\n");

    removable_ = (inquiry_data.removable & 0x80);

    ReadCapacity16CDB read_capacity_cdb = {};
    ReadCapacity16ParameterData read_capacity_data = {};
    read_capacity_cdb.opcode = Opcode::READ_CAPACITY_16;
    read_capacity_cdb.service_action = 0x10;
    read_capacity_cdb.allocation_length = ntohl(sizeof(read_capacity_data));
    status = controller_->ExecuteCommandSync(/*target=*/target_, /*lun=*/lun_,
        /*cdb=*/{&read_capacity_cdb, sizeof(read_capacity_cdb)},
        /*data_out=*/{nullptr, 0},
        /*data_in=*/{&read_capacity_data, sizeof(read_capacity_data)});
    if (status != ZX_OK) {
        return status;
    }

    blocks_ = htobe64(read_capacity_data.returned_logical_block_address);
    block_size_ = ntohl(read_capacity_data.block_length_in_bytes);

    zxlogf(INFO, "%ld blocks of %d bytes\n", blocks_, block_size_);

    return DdkAdd(tag_);
}

void Disk::BlockImplQueue(block_op_t* op, block_impl_queue_callback completion_cb,
                          void* cookie) {
    if ((op->command & BLOCK_OP_MASK) != BLOCK_OP_READ) {
        completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, op);
        return;
    }

    void* data = calloc(op->rw.length, block_size_);
    Read16CDB cdb = {};
    cdb.opcode = Opcode::READ_16;
    cdb.logical_block_address = htobe64(op->rw.offset_dev);
    cdb.transfer_length = htonl(op->rw.length);

    auto status = controller_->ExecuteCommandSync(/*target=*/target_, /*lun=*/lun_,
        /*cdb=*/{&cdb, sizeof(cdb)},
        /*data_out=*/{nullptr, 0},
        /*data_in=*/{data, op->rw.length * block_size_});
    // TODO(ZX-2314): Pass VMO directly to ExecuteCommandSync to skip this copy.
    if (status == ZX_OK) {
        status = zx_vmo_write(op->rw.vmo, data, op->rw.offset_vmo, op->rw.length * block_size_);
    }
    free(data);
    completion_cb(cookie, status, op);
}

Disk::Disk(Controller* controller, zx_device_t* parent, uint8_t target, uint16_t lun)
    : DeviceType(parent), controller_(controller), target_(target), lun_(lun) {
    snprintf(tag_, sizeof(tag_), "scsi-disk-%d-%d", target_, lun_);
}

} // namespace scsi
