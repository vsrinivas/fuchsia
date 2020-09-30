// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "sysconfig-fidl.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/epitaph.h>

#include <fbl/string.h>

#include "device-partitioner.h"
#include "lib/async/dispatcher.h"
#include "src/storage/lib/paver/pave-logging.h"

namespace paver {

namespace {
inline constexpr Arch GetCurrentArch() {
#if defined(__x86_64__)
  return Arch::kX64;
#elif defined(__aarch64__)
  return Arch::kArm64;
#else
#error "Unknown arch"
#endif
}
}  // namespace

void Sysconfig::Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root,
                     zx::channel svc_root, std::shared_ptr<Context> context, zx::channel server) {
  auto device_partitioner = DevicePartitionerFactory::Create(
      devfs_root.duplicate(), std::move(svc_root), GetCurrentArch(), context);
  if (!device_partitioner) {
    ERROR("Unable to initialize a partitioner.\n");
    fidl_epitaph_write(server.get(), ZX_ERR_BAD_STATE);
    return;
  }

  auto res = device_partitioner->FindPartition(PartitionSpec(Partition::kSysconfig));
  if (res.is_error()) {
    ERROR("Unable to find sysconfig-data partition. %s\n", res.status_string());
    fidl_epitaph_write(server.get(), ZX_ERR_NOT_SUPPORTED);
    return;
  }

  auto sysconfig = std::make_unique<Sysconfig>(std::move(res.value()));
  fidl::BindSingleInFlightOnly(dispatcher, std::move(server), std::move(sysconfig));
}

void Sysconfig::Read(ReadCompleter::Sync& completer) {
  LOG("Reading sysconfig-data partition.\n");

  auto status_get_partition_size = partitioner_->GetPartitionSize();
  if (status_get_partition_size.is_error()) {
    completer.ReplyError(status_get_partition_size.error_value());
    return;
  }
  const uint64_t partition_size = status_get_partition_size.value();

  zx::vmo vmo;
  if (auto status = zx::make_status(zx::vmo::create(partition_size, 0, &vmo)); status.is_error()) {
    ERROR("Error creating vmo for sysconfig partition read: %s\n", status.status_string());
    completer.ReplyError(status.error_value());
    return;
  }

  if (auto status = partitioner_->Read(vmo, static_cast<size_t>(partition_size));
      status.is_error()) {
    ERROR("Error writing partition data for sysconfig: %s\n", status.status_string());
    completer.ReplyError(status.error_value());
    return;
  }

  completer.ReplySuccess(::llcpp::fuchsia::mem::Buffer{std::move(vmo), partition_size});
  LOG("Completed successfully\n");
}

void Sysconfig::Write(::llcpp::fuchsia::mem::Buffer payload, WriteCompleter::Sync& completer) {
  LOG("Writing sysconfig-data partition.\n");

  if (auto status = partitioner_->Write(payload.vmo, payload.size); status.is_error()) {
    ERROR("Error writing to sysconfig partition. %s, %lu, \n", status.status_string(),
          payload.size);
    completer.Reply(status.error_value());
  }
  completer.Reply(ZX_OK);
  LOG("Completed successfully\n");
}

void Sysconfig::GetPartitionSize(GetPartitionSizeCompleter::Sync& completer) {
  LOG("Getting sysconfig-data partition size.\n");

  auto status_get_partition_size = partitioner_->GetPartitionSize();
  if (status_get_partition_size.is_error()) {
    ERROR("Error getting partition size\n");
    completer.ReplyError(status_get_partition_size.error_value());
    return;
  }
  const uint64_t partition_size = status_get_partition_size.value();
  completer.ReplySuccess(partition_size);
  LOG("Completed successfully\n");
}

void Sysconfig::Flush(FlushCompleter::Sync& completer) {
  LOG("Flushing sysconfig-data partition\n")

  if (auto status = partitioner_->Flush(); status.is_error()) {
    ERROR("Error flushing sysconfig-data partition. %s\n", status.status_string());
    completer.Reply(status.error_value());
  }
  completer.Reply(ZX_OK);
  LOG("Completed successfully\n");
}

void Sysconfig::Wipe(WipeCompleter::Sync& completer) {
  LOG("Wiping sysconfig-data partition\n")
  auto get_ptn_size_status = partitioner_->GetPartitionSize();
  if (get_ptn_size_status.is_error()) {
    ERROR("Failed to get partition size: %s\n", get_ptn_size_status.status_string());
    completer.Reply(get_ptn_size_status.error_value());
    return;
  }
  auto ptn_size = get_ptn_size_status.value();

  zx::vmo zeros;
  // Assuem all bytes are 0 when intialized
  if (auto status = zx::vmo::create(ptn_size, 0, &zeros); status != ZX_OK) {
    ERROR("Failed to create VMO: %s\n", zx_status_get_string(status));
    completer.Reply(status);
    return;
  }

  if (auto status = partitioner_->Write(zeros, ptn_size); status.is_error()) {
    ERROR("Failed to write to partition. %s\n", status.status_string());
    completer.Reply(status.error_value());
    return;
  }

  completer.Reply(ZX_OK);
  LOG("Completed successfully\n");
}

}  // namespace paver
