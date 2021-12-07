// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svcfs-service.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/zx/job.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>

#include <fbl/algorithm.h>

#include "util.h"

namespace {

zx_status_t FactoryItemsGet(void* ctx, uint32_t extra, fidl_txn_t* txn) {
  auto map = static_cast<bootsvc::FactoryItemMap*>(ctx);
  auto it = map->find(extra);
  if (it == map->end()) {
    return fuchsia_boot_FactoryItemsGet_reply(txn, ZX_HANDLE_INVALID, 0);
  }

  const zx::vmo& vmo = it->second.vmo;
  uint32_t length = it->second.length;
  zx::vmo payload;
  zx_status_t status =
      vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & ~(ZX_RIGHT_WRITE | ZX_RIGHT_SET_PROPERTY), &payload);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to duplicate handle for factory item VMO: %s",
           zx_status_get_string(status));
    return status;
  }

  return fuchsia_boot_FactoryItemsGet_reply(txn, payload.release(), length);
}

constexpr fuchsia_boot_FactoryItems_ops kFactoryItemsOps = {
    .Get = FactoryItemsGet,
};

struct ItemsData {
  zx::vmo vmo;
  bootsvc::ItemMap map;
  bootsvc::BootloaderFileMap bootloader_file_map;
};

zx_status_t CopyItem(const zx::vmo& vmo, const bootsvc::ItemValue& item, zx::vmo* out_vmo) {
  auto buf = std::make_unique<uint8_t[]>(item.length);
  zx_status_t status = vmo.read(buf.get(), item.offset, item.length);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to read from boot image VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  zx::vmo payload;
  status = zx::vmo::create(item.length, 0, &payload);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to create payload VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  status = payload.write(buf.get(), 0, item.length);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to write to payload VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  *out_vmo = std::move(payload);
  return ZX_OK;
}

zx_status_t ItemsGet(void* ctx, uint32_t type, uint32_t extra, fidl_txn_t* txn) {
  auto data = static_cast<const ItemsData*>(ctx);
  auto it = data->map.find(bootsvc::ItemKey{type, extra});
  if (it == data->map.end() || it->second.empty()) {
    return fuchsia_boot_ItemsGet_reply(txn, ZX_HANDLE_INVALID, 0);
  }

  // TODO(fxbug.dev/34597): As detailed in this bug, fuchisa.boot.Items makes
  // invalid assumptions about the ZBI spec: in particular, that (type, extra)
  // is a unique key among items. Until that is sorted out, just return the
  // last of possibly many items with the given key (last conventionally wins
  // with ZBI items).
  auto& item = it->second.back();

  zx::vmo payload;
  zx_status_t status = CopyItem(data->vmo, item, &payload);
  if (status) {
    return status;
  }

  return fuchsia_boot_ItemsGet_reply(txn, payload.release(), item.length);
}

zx_status_t BootloaderFilesGet(void* ctx, const char* filename_data, size_t filename_size,
                               fidl_txn_t* txn) {
  auto data = static_cast<const ItemsData*>(ctx);
  auto it = data->bootloader_file_map.find(std::string(filename_data, filename_size));
  if (it == data->bootloader_file_map.end()) {
    return fuchsia_boot_ItemsGetBootloaderFile_reply(txn, ZX_HANDLE_INVALID);
  }
  auto& item = it->second;

  zx::vmo vmo;
  zx_status_t status = CopyItem(data->vmo, item, &vmo);
  if (status) {
    return status;
  }

  uint64_t size = item.length;
  status = vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size));
  if (status != ZX_OK) {
    printf("bootsvc: Failed to set content size of payload VMO: %s\n",
           zx_status_get_string(status));
    return status;
  }

  return fuchsia_boot_ItemsGetBootloaderFile_reply(txn, vmo.release());
}

constexpr fuchsia_boot_Items_ops kItemsOps = {
    .Get = ItemsGet,
    .GetBootloaderFile = BootloaderFilesGet,
};

}  // namespace

namespace bootsvc {

fbl::RefPtr<SvcfsService> SvcfsService::Create(async_dispatcher_t* dispatcher) {
  return fbl::AdoptRef(new SvcfsService(dispatcher));
}

SvcfsService::SvcfsService(async_dispatcher_t* dispatcher)
    : vfs_(dispatcher), root_(fbl::MakeRefCounted<fs::PseudoDir>()) {}

void SvcfsService::AddService(const char* service_name, fbl::RefPtr<fs::Service> service) {
  root_->AddEntry(service_name, std::move(service));
}

zx_status_t SvcfsService::CreateRootConnection(zx::channel* out) {
  return CreateVnodeConnection(&vfs_, root_, fs::Rights::ReadWrite(), out);
}

fbl::RefPtr<fs::Service> CreateFactoryItemsService(async_dispatcher_t* dispatcher,
                                                   FactoryItemMap map) {
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, map = std::move(map)](zx::channel channel) mutable {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_FactoryItems_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, &map, &kFactoryItemsOps);
      });
}

fbl::RefPtr<fs::Service> CreateItemsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                            ItemMap map, BootloaderFileMap bootloader_file_map) {
  ItemsData data{std::move(vmo), std::move(map), std::move(bootloader_file_map)};
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, data = std::move(data)](zx::channel channel) mutable {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, &data, &kItemsOps);
      });
}

}  // namespace bootsvc
