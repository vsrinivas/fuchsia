// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/c/fidl.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/txn_header.h>

#include "coordinator.h"
#include "log.h"

zx_status_t dh_send_create_device(Device* dev, const fbl::RefPtr<Devhost>& dh,
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
                       fuchsia_device_manager_DevhostControllerCreateDeviceGenOrdinal);

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

  zx_handle_t handles[4] = {coordinator_rpc.release(), device_controller_rpc.release(),
                            driver.release()};
  uint32_t num_handles = 3;

  if (rpc_proxy.is_valid()) {
    handles[num_handles++] = rpc_proxy.release();
  }

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(handles, num_handles, num_handles));
  return msg.Write(dh->hrpc().get(), 0);
}

zx_status_t dh_send_create_device_stub(Device* dev, const fbl::RefPtr<Devhost>& dh,
                                       zx::channel coordinator_rpc,
                                       zx::channel device_controller_rpc, uint32_t protocol_id) {
  FIDL_ALIGNDECL char
      wr_bytes[sizeof(fuchsia_device_manager_DevhostControllerCreateDeviceStubRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req = builder.New<fuchsia_device_manager_DevhostControllerCreateDeviceStubRequest>();
  ZX_ASSERT(req != nullptr);
  fidl_init_txn_header(&req->hdr, 1,
                       fuchsia_device_manager_DevhostControllerCreateDeviceStubGenOrdinal);
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  req->coordinator_rpc = FIDL_HANDLE_PRESENT;
  req->device_controller_rpc = FIDL_HANDLE_PRESENT;
  req->protocol_id = protocol_id;
  req->local_device_id = dev->local_id();

  zx_handle_t handles[] = {coordinator_rpc.release(), device_controller_rpc.release()};
  fidl::Message msg(builder.Finalize(),
                    fidl::HandlePart(handles, fbl::count_of(handles), fbl::count_of(handles)));
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
    log(ERROR, "driver_manager: init done '%s'\n", dev->name().data());
    dev->CompleteInit(status);
  });
  return ZX_OK;
}

zx_status_t dh_send_suspend(Device* dev_ptr, uint32_t flags) {
  auto dev = fbl::RefPtr(dev_ptr);
  dev->device_controller()->Suspend(flags, [dev](zx_status_t status) {
    log(INFO, "driver_manager: suspended name='%s' status %d\n", dev->name().data(), status);
    dev->CompleteSuspend(status);
  });
  return ZX_OK;
}

zx_status_t dh_send_resume(Device* dev_ptr, uint32_t target_system_state) {
  auto dev = fbl::RefPtr(dev_ptr);
  dev_ptr->device_controller()->Resume(target_system_state, [dev](zx_status_t status) {
    log(INFO, "driver_manager: resumed dev %p name='%s'\n", dev.get(), dev->name().data());
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
        log(ERROR, "driver_manager: unbind done '%s'\n", dev->name().data());
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
        log(ERROR, "driver_manager: remove done '%s'\n", dev->name().data());
        cb();
      });
  return ZX_OK;
}

zx_status_t dh_send_create_composite_device(const fbl::RefPtr<Devhost>& dh,
                                            const Device* composite_dev,
                                            const CompositeDevice& composite,
                                            const uint64_t* fragment_local_ids,
                                            zx::channel coordinator_rpc,
                                            zx::channel device_controller_rpc) {
  size_t fragments_size = composite.fragments_count() * sizeof(uint64_t);
  size_t name_size = composite.name().size();
  uint32_t wr_num_bytes = static_cast<uint32_t>(
      sizeof(fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequest) +
      FIDL_ALIGN(fragments_size) + FIDL_ALIGN(name_size));
  FIDL_ALIGNDECL char wr_bytes[wr_num_bytes];
  fidl::Builder builder(wr_bytes, wr_num_bytes);

  auto req = builder.New<fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequest>();
  uint64_t* fragments_data =
      builder.NewArray<uint64_t>(static_cast<uint32_t>(composite.fragments_count()));
  char* name_data = builder.NewArray<char>(static_cast<uint32_t>(name_size));
  ZX_ASSERT(req != nullptr && fragments_data != nullptr && name_data != nullptr);
  // TODO(teisenbe): Allocate and track txids
  zx_txid_t txid = 1;
  fidl_init_txn_header(&req->hdr, txid,
                       fuchsia_device_manager_DevhostControllerCreateCompositeDeviceGenOrdinal);

  req->coordinator_rpc = FIDL_HANDLE_PRESENT;
  req->device_controller_rpc = FIDL_HANDLE_PRESENT;

  req->fragments.count = composite.fragments_count();
  req->fragments.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(fragments_data, fragment_local_ids, fragments_size);

  req->name.size = name_size;
  req->name.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(name_data, composite.name().data(), name_size);

  req->local_device_id = composite_dev->local_id();

  zx_handle_t handles[2] = {coordinator_rpc.release(), device_controller_rpc.release()};
  uint32_t num_handles = 2;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(handles, num_handles, num_handles));
  return msg.Write(dh->hrpc().get(), 0);
}
