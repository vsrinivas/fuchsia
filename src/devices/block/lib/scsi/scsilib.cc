// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/c/banjo.h>
#include <lib/scsi/scsilib.h>
#include <netinet/in.h>
#include <zircon/process.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

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
  // data.lun_list_length is the number of bytes of LUN structures.
  return ntohl(data.lun_list_length) / 8;
}

zx_status_t Disk::Create(Controller* controller, zx_device_t* parent, uint8_t target, uint16_t lun,
                         uint32_t max_xfer_size) {
  fbl::AllocChecker ac;
  auto* const disk = new (&ac) scsi::Disk(controller, parent, /*target=*/target, /*lun=*/lun);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  disk->max_xfer_size_ = max_xfer_size;
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
  zxlogf(INFO, "");

  removable_ = (inquiry_data.removable & 0x80);

  ReadCapacity16CDB read_capacity_cdb = {};
  ReadCapacity16ParameterData read_capacity_data = {};
  read_capacity_cdb.opcode = Opcode::READ_CAPACITY_16;
  read_capacity_cdb.service_action = 0x10;
  read_capacity_cdb.allocation_length = ntohl(sizeof(read_capacity_data));
  status = controller_->ExecuteCommandSync(
      /*target=*/target_, /*lun=*/lun_,
      /*cdb=*/{&read_capacity_cdb, sizeof(read_capacity_cdb)},
      /*data_out=*/{nullptr, 0},
      /*data_in=*/{&read_capacity_data, sizeof(read_capacity_data)});
  if (status != ZX_OK) {
    return status;
  }

  blocks_ = htobe64(read_capacity_data.returned_logical_block_address) + 1;
  block_size_ = ntohl(read_capacity_data.block_length_in_bytes);

  zxlogf(INFO, "%ld blocks of %d bytes", blocks_, block_size_);

  return DdkAdd(tag_);
}

struct scsilib_cb {
  void* ScsiDisk;
  void* blk_cookie;
  block_impl_queue_callback completion_cb;
  block_op_t* op;
  zx_vaddr_t mapped_addr;
  void* data;
};

static void ScsiLibCompletionCb(void* c, zx_status_t status) {
  auto cookie = reinterpret_cast<struct scsilib_cb*>(c);
  auto disk = reinterpret_cast<class Disk*>(cookie->ScsiDisk);
  uint64_t length = cookie->op->rw.length * disk->BlockSize();
  uint64_t vmo_offset = cookie->op->rw.offset_vmo * disk->BlockSize();

  if (cookie->mapped_addr != reinterpret_cast<zx_vaddr_t>(nullptr)) {
    status = zx_vmar_unmap(zx_vmar_root_self(), cookie->mapped_addr, length);
  } else {
    auto op_type = cookie->op->command & BLOCK_OP_MASK;

    if (op_type == BLOCK_OP_READ) {
      if (status == ZX_OK) {
        status = zx_vmo_write(cookie->op->rw.vmo, cookie->data, vmo_offset, length);
      }
    }
    free(cookie->data);
  }
  cookie->completion_cb(cookie->blk_cookie, status, cookie->op);
  delete cookie;
}

void Disk::BlockImplQueue(block_op_t* op, block_impl_queue_callback completion_cb, void* cookie) {
  auto op_type = op->command & BLOCK_OP_MASK;
  if (!(op_type == BLOCK_OP_READ || op_type == BLOCK_OP_WRITE || op_type == BLOCK_OP_FLUSH)) {
    completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, op);
    return;
  }
  // To use zx_vmar_map, offset, length must be page aligned. If it isn't (uncommon),
  // allocate a temp buffer and do a copy.
  uint64_t length = op->rw.length * block_size_;
  uint64_t vmo_offset = op->rw.offset_vmo * block_size_;
  zx_vaddr_t mapped_addr = reinterpret_cast<zx_vaddr_t>(nullptr);
  void* data = nullptr;  // Quiet compiler.
  zx_status_t status;
  if ((length > 0) && ((length % PAGE_SIZE) == 0) && ((vmo_offset % PAGE_SIZE) == 0)) {
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, op->rw.vmo,
                         vmo_offset, length, &mapped_addr);
    if (status != ZX_OK) {
      completion_cb(cookie, status, op);
      return;
    }
    data = reinterpret_cast<void*>(mapped_addr);
  } else {
    data = calloc(op->rw.length, block_size_);
    if (op_type == BLOCK_OP_WRITE) {
      status = zx_vmo_read(op->rw.vmo, data, vmo_offset, length);
      if (status != ZX_OK) {
        free(data);
        completion_cb(cookie, status, op);
        return;
      }
    }
  }
  auto scsilib_cookie = reinterpret_cast<struct scsilib_cb*>(new scsilib_cb);
  scsilib_cookie->ScsiDisk = this;
  scsilib_cookie->blk_cookie = cookie;
  scsilib_cookie->completion_cb = completion_cb;
  scsilib_cookie->op = op;
  scsilib_cookie->mapped_addr = mapped_addr;
  scsilib_cookie->data = data;
  if (op_type == BLOCK_OP_READ) {
    Read16CDB cdb = {};
    cdb.opcode = Opcode::READ_16;
    cdb.logical_block_address = htobe64(op->rw.offset_dev);
    cdb.transfer_length = htonl(op->rw.length);
    status = controller_->ExecuteCommandAsync(/*target=*/target_, /*lun=*/lun_,
                                              /*cdb=*/{&cdb, sizeof(cdb)},
                                              /*data_out=*/{nullptr, 0},
                                              /*data_in=*/{data, length}, ScsiLibCompletionCb,
                                              scsilib_cookie);
  } else if (op_type == BLOCK_OP_WRITE) {
    Write16CDB cdb = {};
    cdb.opcode = Opcode::WRITE_16;
    cdb.logical_block_address = htobe64(op->rw.offset_dev);
    cdb.transfer_length = htonl(op->rw.length);
    status = controller_->ExecuteCommandAsync(/*target=*/target_, /*lun=*/lun_,
                                              /*cdb=*/{&cdb, sizeof(cdb)},
                                              /*data_out=*/{data, length},
                                              /*data_in=*/{nullptr, 0}, ScsiLibCompletionCb,
                                              scsilib_cookie);
  } else {
    Synchronize10CDB cdb = {};
    cdb.opcode = Opcode::SYNCHRONIZE_CACHE_10;
    // Prefer writing to storage medium (instead of nv cache) and return only
    // after completion of operation.
    cdb.syncnv_immed = 0;
    // Ideally this would flush specific blocks, but several platforms don't
    // support this functionality, so just synchronize the whole disk.
    cdb.logical_block_address = 0;
    cdb.num_blocks = 0;
    status = controller_->ExecuteCommandAsync(/*target=*/target_, /*lun=*/lun_,
                                              /*cdb=*/{&cdb, sizeof(cdb)},
                                              /*data_out=*/{nullptr, 0},
                                              /*data_in=*/{nullptr, 0}, ScsiLibCompletionCb,
                                              scsilib_cookie);
  }
}

Disk::Disk(Controller* controller, zx_device_t* parent, uint8_t target, uint16_t lun)
    : DeviceType(parent), controller_(controller), target_(target), lun_(lun) {
  snprintf(tag_, sizeof(tag_), "scsi-disk-%d-%d", target_, lun_);
}

}  // namespace scsi
