// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/c/fidl.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/txn_header.h>
#include <zircon/rights.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "coordinator.h"
#include "src/devices/lib/log/log.h"

zx_status_t dh_send_create_device(Device* dev, const fbl::RefPtr<DriverHost>& dh,
                                  zx::channel coordinator_rpc, zx::channel device_controller_rpc,
                                  zx::vmo driver, const char* args, zx::handle rpc_proxy) {
  size_t driver_path_size = dev->libname().size();
  size_t args_size = strlen(args);
  uint32_t wr_num_bytes =
      static_cast<uint32_t>(sizeof(fuchsia_device_manager_DevhostControllerCreateDeviceRequest) +
                            FIDL_ALIGN(driver_path_size) + FIDL_ALIGN(args_size));
  FIDL_ALIGNDECL char wr_bytes[wr_num_bytes];
  fidl::Builder builder(wr_bytes, wr_num_bytes);

  auto req = builder.New<fuchsia_device_manager_DevhostControllerCreateDeviceRequest>();
  char* driver_path_data = builder.NewArray<char>(static_cast<uint32_t>(driver_path_size));
  char* args_data = builder.NewArray<char>(static_cast<uint32_t>(args_size));
  ZX_ASSERT(req != nullptr && driver_path_data != nullptr && args_data != nullptr);
  // TODO(teisenbe): Allocate and track txids
  zx_txid_t txid = 1;
  fidl_init_txn_header(&req->hdr, txid,
                       fuchsia_device_manager_DevhostControllerCreateDeviceOrdinal);

  req->coordinator_rpc = FIDL_HANDLE_PRESENT;
  req->device_controller_rpc = FIDL_HANDLE_PRESENT;

  req->driver_path.size = driver_path_size;
  req->driver_path.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(driver_path_data, dev->libname().data(), driver_path_size);

  req->driver = FIDL_HANDLE_PRESENT;
  req->parent_proxy = rpc_proxy.is_valid() ? FIDL_HANDLE_PRESENT : FIDL_HANDLE_ABSENT;

  req->proxy_args.size = args_size;
  req->proxy_args.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(args_data, args, args_size);

  req->local_device_id = dev->local_id();

  // TODO(fxbug.dev/41920) Specify more specific rights.
  zx_handle_disposition_t handles[4] = {
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = coordinator_rpc.release(),
          .type = ZX_OBJ_TYPE_CHANNEL,
          .rights = ZX_DEFAULT_CHANNEL_RIGHTS,
          .result = ZX_OK,
      },
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = device_controller_rpc.release(),
          .type = ZX_OBJ_TYPE_NONE,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = driver.release(),
          .type = ZX_OBJ_TYPE_NONE,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
  };
  uint32_t num_handles = 3;

  if (rpc_proxy.is_valid()) {
    handles[num_handles++] = zx_handle_disposition_t{
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = rpc_proxy.release(),
        .type = ZX_OBJ_TYPE_NONE,
        .rights = ZX_RIGHT_SAME_RIGHTS,
        .result = ZX_OK,
    };
  }

  fidl::HLCPPOutgoingMessage msg(builder.Finalize(),
                                 fidl::HandleDispositionPart(handles, num_handles, num_handles));
  return msg.Write(dh->hrpc().get(), 0);
}

zx_status_t dh_send_create_device_stub(Device* dev, const fbl::RefPtr<DriverHost>& dh,
                                       zx::channel coordinator_rpc,
                                       zx::channel device_controller_rpc, uint32_t protocol_id) {
  FIDL_ALIGNDECL char
      wr_bytes[sizeof(fuchsia_device_manager_DevhostControllerCreateDeviceStubRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req = builder.New<fuchsia_device_manager_DevhostControllerCreateDeviceStubRequest>();
  ZX_ASSERT(req != nullptr);
  fidl_init_txn_header(&req->hdr, 1,
                       fuchsia_device_manager_DevhostControllerCreateDeviceStubOrdinal);
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  req->coordinator_rpc = FIDL_HANDLE_PRESENT;
  req->device_controller_rpc = FIDL_HANDLE_PRESENT;
  req->protocol_id = protocol_id;
  req->local_device_id = dev->local_id();

  // TODO(fxbug.dev/41920) Specify more specific rights.
  zx_handle_disposition_t handles[] = {
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = coordinator_rpc.release(),
          .type = ZX_OBJ_TYPE_NONE,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = device_controller_rpc.release(),
          .type = ZX_OBJ_TYPE_NONE,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
  };
  fidl::HLCPPOutgoingMessage msg(
      builder.Finalize(),
      fidl::HandleDispositionPart(handles, std::size(handles), std::size(handles)));
  return msg.Write(dh->hrpc().get(), 0);
}

zx_status_t dh_send_bind_driver(Device* dev_ptr, const char* libname, zx::vmo driver,
                                fit::function<void(zx_status_t, zx::channel)> cb) {
  auto dev = fbl::RefPtr(dev_ptr);
  dev_ptr->device_controller()->BindDriver(libname, std::move(driver), std::move(cb));
  return ZX_OK;
}

zx_status_t dh_send_connect_proxy(const Device* dev, zx::channel proxy) {
  dev->device_controller()->ConnectProxy(std::move(proxy));
  return ZX_OK;
}

zx_status_t dh_send_init(Device* dev_ptr) {
  auto dev = fbl::RefPtr(dev_ptr);
  dev->device_controller()->Init([dev](zx_status_t status) {
    LOGF(INFO, "Initialized device %p '%s': %s", dev.get(), dev->name().data(),
         zx_status_get_string(status));
    dev->CompleteInit(status);
  });
  return ZX_OK;
}

zx_status_t dh_send_suspend(Device* dev_ptr, uint32_t flags) {
  auto dev = fbl::RefPtr(dev_ptr);
  dev->device_controller()->Suspend(flags, [dev](zx_status_t status) {
    if (status == ZX_OK) {
      LOGF(DEBUG, "Suspended device %p '%s'successfully", dev.get(), dev->name().data());
    } else {
      LOGF(ERROR, "Failed to suspended device %p '%s': %s", dev.get(), dev->name().data(),
           zx_status_get_string(status));
    }
    dev->CompleteSuspend(status);
  });
  return ZX_OK;
}

zx_status_t dh_send_resume(Device* dev_ptr, uint32_t target_system_state) {
  auto dev = fbl::RefPtr(dev_ptr);
  dev_ptr->device_controller()->Resume(target_system_state, [dev](zx_status_t status) {
    LOGF(INFO, "Resumed device %p '%s': %s", dev.get(), dev->name().data(),
         zx_status_get_string(status));
    dev->CompleteResume(status);
  });
  return ZX_OK;
}

zx_status_t dh_send_complete_compatibility_tests(const Device* dev, zx_status_t status) {
  dev->device_controller()->CompleteCompatibilityTests(
      static_cast<fuchsia::device::manager::CompatibilityTestStatus>(status));
  return ZX_OK;
}

zx_status_t dh_send_unbind(Device* dev_ptr) {
  auto dev = fbl::RefPtr(dev_ptr);
  dev->device_controller()->Unbind(
      [dev](fuchsia::device::manager::DeviceController_Unbind_Result status) {
        LOGF(INFO, "Unbound device %p '%s': %s", dev.get(), dev->name().data(),
             zx_status_get_string(status.is_err() ? status.err() : ZX_OK));
        dev->CompleteUnbind();
      });
  return ZX_OK;
}

zx_status_t dh_send_complete_removal(Device* dev_ptr, fit::function<void()> cb) {
  auto dev = fbl::RefPtr(dev_ptr);
  dev->set_state(Device::State::kUnbinding);
  dev->device_controller()->CompleteRemoval(
      [dev, cb = std::move(cb)](
          fuchsia::device::manager::DeviceController_CompleteRemoval_Result status) {
        LOGF(INFO, "Removed device %p '%s': %s", dev.get(), dev->name().data(),
             zx_status_get_string(status.is_err() ? status.err() : ZX_OK));
        cb();
      });
  return ZX_OK;
}

zx_status_t dh_send_create_composite_device(const fbl::RefPtr<DriverHost>& dh,
                                            const Device* composite_dev,
                                            const CompositeDevice& composite,
                                            const std::pair<std::string_view, uint64_t>* fragments,
                                            zx::channel coordinator_rpc,
                                            zx::channel device_controller_rpc) {
  size_t fragments_size = composite.fragments_count() *
                          (FIDL_ALIGN(sizeof(fuchsia_device_manager_Fragment)) +
                           (FIDL_ALIGN(sizeof(char) * fuchsia_device_manager_FRAGMENT_NAME_MAX)));
  size_t name_size = composite.name().size();
  uint32_t wr_num_bytes = static_cast<uint32_t>(
      sizeof(fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequest) +
      FIDL_ALIGN(fragments_size) + FIDL_ALIGN(name_size));
  FIDL_ALIGNDECL char wr_bytes[wr_num_bytes];
  fidl::Builder builder(wr_bytes, wr_num_bytes);

  auto req = builder.New<fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequest>();
  fuchsia_device_manager_Fragment* fragments_data =
      builder.NewArray<fuchsia_device_manager_Fragment>(
          static_cast<uint32_t>(composite.fragments_count()));
  ZX_ASSERT(req != nullptr && fragments_data != nullptr);
  // TODO(teisenbe): Allocate and track txids
  zx_txid_t txid = 1;
  fidl_init_txn_header(&req->hdr, txid,
                       fuchsia_device_manager_DevhostControllerCreateCompositeDeviceOrdinal);

  req->coordinator_rpc = FIDL_HANDLE_PRESENT;
  req->device_controller_rpc = FIDL_HANDLE_PRESENT;

  req->fragments.count = composite.fragments_count();
  req->fragments.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  for (size_t i = 0; i < composite.fragments_count(); i++) {
    fragments_data[i].id = fragments[i].second;
    fragments_data[i].name.size = fragments[i].first.size();
    char* name_data = builder.NewArray<char>(static_cast<uint32_t>(fragments[i].first.size()));
    ZX_ASSERT(name_data != nullptr);
    fragments_data[i].name.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    fragments[i].first.copy(name_data, fragments[i].first.size());
  }

  char* name_data = builder.NewArray<char>(static_cast<uint32_t>(name_size));
  ZX_ASSERT(name_data != nullptr);
  req->name.size = name_size;
  req->name.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(name_data, composite.name().data(), name_size);

  req->local_device_id = composite_dev->local_id();

  zx_handle_disposition_t handles[2] = {
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = coordinator_rpc.release(),
          .type = ZX_OBJ_TYPE_CHANNEL,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = device_controller_rpc.release(),
          .type = ZX_OBJ_TYPE_CHANNEL,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
  };
  uint32_t num_handles = 2;

  fidl::HLCPPOutgoingMessage msg(builder.Finalize(),
                                 fidl::HandleDispositionPart(handles, num_handles, num_handles));
  return msg.Write(dh->hrpc().get(), 0);
}
