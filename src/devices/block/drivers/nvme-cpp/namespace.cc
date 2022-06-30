// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme-cpp/namespace.h"

#include <fuchsia/hardware/block/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/syscalls.h>

#include <ddktl/device.h>
#include <fbl/string_printf.h>

#include "src/devices/block/drivers/nvme-cpp/commands/identify.h"
#include "src/devices/block/drivers/nvme-cpp/nvme.h"

namespace nvme {

zx_status_t Namespace::Create(Nvme* controller, uint32_t id) {
  if (id == 0 || id == ~0u) {
    zxlogf(ERROR, "Attempted to create namespace with invalid id 0x%x", id);
    return ZX_ERR_INVALID_ARGS;
  }

  auto dev = std::make_unique<Namespace>(controller->zxdev(), controller, id);
  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // The DDK takes ownership of the device.
    __UNUSED auto unused = dev.release();
  }

  return status;
}

zx_status_t Namespace::Bind() {
  auto name = fbl::StringPrintf("namespace-%u", namespace_id_);
  return DdkAdd(ddk::DeviceAddArgs(name.data()));
}

void Namespace::DdkInit(ddk::InitTxn txn) {
  zx::vmo data;
  zx_status_t status = zx::vmo::create(zx_system_get_page_size(), 0, &data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate namespace ID VMO: %s", zx_status_get_string(status));
    txn.Reply(status);
    return;
  }
  auto promise = controller_->IdentifyNamespace(namespace_id_, data);
  if (promise.is_error()) {
    zxlogf(ERROR, "Failed to identify namespace: %s", promise.status_string());
    txn.Reply(promise.error_value());
    return;
  }

  controller_->executor().schedule_task(
      promise->then([this, txn = std::move(txn), data = std::move(data)](
                        fpromise::result<Completion, Completion>& result) mutable {
        if (result.is_error()) {
          zxlogf(ERROR, "Failed to identify namespace: status type 0x%x code 0x%x",
                 result.error().status_code_type(), result.error().status_code());
          txn.Reply(ZX_ERR_INTERNAL);
          return;
        }

        fzl::VmoMapper mapper;
        zx_status_t status = mapper.Map(data);
        if (status != ZX_OK) {
          zxlogf(ERROR, "Failed to map namespace identification data: %s",
                 zx_status_get_string(status));
          txn.Reply(status);
          return;
        }

        IdentifyNvmeNamespace* id = static_cast<IdentifyNvmeNamespace*>(mapper.start());

        auto& fmt = id->lba_formats[id->lba_format_index()];
        zxlogf(INFO, "Current LBA format has LBAs of size %u (log2 %d), perf %d, metadata size %d",
               fmt.lba_data_size_bytes(), fmt.lba_data_size_log2(), fmt.relative_performance(),
               fmt.metadata_size_bytes());
        if (fmt.metadata_size_bytes() != 0) {
          zxlogf(ERROR, "NVMe drive uses metadata (%u bytes), which we do not support. Aborting.",
                 fmt.metadata_size_bytes());
          txn.Reply(ZX_ERR_NOT_SUPPORTED);
          return;
        }
        lba_size_ = fmt.lba_data_size_bytes();
        lba_count_ = id->n_sze;

        txn.Reply(ZX_OK);
      }));
}

void Namespace::BlockImplQuery(block_info_t* out_info, uint64_t* out_block_op_size) {
  *out_block_op_size = sizeof(block_op_t);
  out_info->block_size = lba_size_;
  out_info->block_count = lba_count_;
  out_info->max_transfer_size = controller_->max_transfer_size();
  out_info->flags = 0;
}

void Namespace::BlockImplQueue(block_op_t* txn, block_impl_queue_callback callback, void* cookie) {
  callback(cookie, ZX_ERR_NOT_SUPPORTED, txn);
}

}  // namespace nvme
