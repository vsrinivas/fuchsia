// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/c/fidl.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_part.h>

#include "coordinator.h"
#include "log.h"

namespace devmgr {

zx_status_t dh_send_create_device(Device* dev, Devhost* dh, zx::channel rpc, zx::vmo driver,
                                  const char* args, zx::handle rpc_proxy) {
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
  req->hdr.ordinal = fuchsia_device_manager_DevhostControllerCreateDeviceOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  req->rpc = FIDL_HANDLE_PRESENT;

  req->driver_path.size = driver_path_size;
  req->driver_path.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(driver_path_data, dev->libname().data(), driver_path_size);

  req->driver = FIDL_HANDLE_PRESENT;
  req->parent_proxy = rpc_proxy.is_valid() ? FIDL_HANDLE_PRESENT : FIDL_HANDLE_ABSENT;

  req->proxy_args.size = args_size;
  req->proxy_args.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(args_data, args, args_size);

  req->local_device_id = dev->local_id();

  zx_handle_t handles[3] = {rpc.release(), driver.release()};
  uint32_t num_handles = 2;

  if (rpc_proxy.is_valid()) {
    handles[num_handles++] = rpc_proxy.release();
  }

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(handles, num_handles, num_handles));
  return msg.Write(dh->hrpc(), 0);
}

zx_status_t dh_send_create_device_stub(Device* dev, Devhost* dh, zx::channel rpc,
                                       uint32_t protocol_id) {
  FIDL_ALIGNDECL char
      wr_bytes[sizeof(fuchsia_device_manager_DevhostControllerCreateDeviceStubRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req = builder.New<fuchsia_device_manager_DevhostControllerCreateDeviceStubRequest>();
  ZX_ASSERT(req != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DevhostControllerCreateDeviceStubOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  req->rpc = FIDL_HANDLE_PRESENT;
  req->protocol_id = protocol_id;
  req->local_device_id = dev->local_id();

  zx_handle_t handles[] = {rpc.release()};
  fidl::Message msg(builder.Finalize(),
                    fidl::HandlePart(handles, fbl::count_of(handles), fbl::count_of(handles)));
  return msg.Write(dh->hrpc(), 0);
}

zx_status_t dh_send_bind_driver(const Device* dev, const char* libname, zx::vmo driver) {
  size_t libname_size = strlen(libname);
  uint32_t wr_num_bytes = static_cast<uint32_t>(
      sizeof(fuchsia_device_manager_DeviceControllerBindDriverRequest) + FIDL_ALIGN(libname_size));
  FIDL_ALIGNDECL char wr_bytes[wr_num_bytes];
  fidl::Builder builder(wr_bytes, wr_num_bytes);

  auto req = builder.New<fuchsia_device_manager_DeviceControllerBindDriverRequest>();
  char* libname_data = builder.NewArray<char>(static_cast<uint32_t>(libname_size));
  ZX_ASSERT(req != nullptr && libname_data != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DeviceControllerBindDriverOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  req->driver_path.size = libname_size;
  req->driver_path.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(libname_data, libname, libname_size);

  req->driver = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {driver.release()};
  fidl::Message msg(builder.Finalize(),
                    fidl::HandlePart(handles, fbl::count_of(handles), fbl::count_of(handles)));
  return msg.Write(dev->channel()->get(), 0);
}

zx_status_t dh_send_connect_proxy(const Device* dev, zx::channel proxy) {
  FIDL_ALIGNDECL char wr_bytes[sizeof(fuchsia_device_manager_DeviceControllerConnectProxyRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req = builder.New<fuchsia_device_manager_DeviceControllerConnectProxyRequest>();
  ZX_ASSERT(req != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DeviceControllerConnectProxyOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  req->shadow = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[] = {proxy.release()};
  fidl::Message msg(builder.Finalize(),
                    fidl::HandlePart(handles, fbl::count_of(handles), fbl::count_of(handles)));
  return msg.Write(dev->channel()->get(), 0);
}

zx_status_t dh_send_suspend(const Device* dev, uint32_t flags) {
  FIDL_ALIGNDECL char wr_bytes[sizeof(fuchsia_device_manager_DeviceControllerSuspendRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req = builder.New<fuchsia_device_manager_DeviceControllerSuspendRequest>();
  ZX_ASSERT(req != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DeviceControllerSuspendOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;
  req->flags = flags;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
  return msg.Write(dev->channel()->get(), 0);
}

zx_status_t dh_send_resume(const Device* dev, uint32_t target_system_state) {
  FIDL_ALIGNDECL char wr_bytes[sizeof(fuchsia_device_manager_DeviceControllerResumeRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req = builder.New<fuchsia_device_manager_DeviceControllerResumeRequest>();
  ZX_ASSERT(req != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DeviceControllerResumeOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;
  req->target_system_state = target_system_state;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
  return msg.Write(dev->channel()->get(), 0);
}

zx_status_t dh_send_complete_compatibility_tests(const Device* dev, zx_status_t status) {
  FIDL_ALIGNDECL char
      wr_bytes[sizeof(fuchsia_device_manager_DeviceControllerCompleteCompatibilityTestsRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req =
      builder.New<fuchsia_device_manager_DeviceControllerCompleteCompatibilityTestsRequest>();
  ZX_ASSERT(req != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DeviceControllerCompleteCompatibilityTestsOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;
  req->status = status;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
  return msg.Write(dev->channel()->get(), 0);
}

zx_status_t dh_send_unbind(const Device* dev) {
  FIDL_ALIGNDECL char wr_bytes[sizeof(fuchsia_device_manager_DeviceControllerUnbindRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req = builder.New<fuchsia_device_manager_DeviceControllerUnbindRequest>();
  ZX_ASSERT(req != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DeviceControllerUnbindOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
  return msg.Write(dev->channel()->get(), 0);
}

zx_status_t dh_send_complete_removal(const Device* dev) {
  FIDL_ALIGNDECL char
      wr_bytes[sizeof(fuchsia_device_manager_DeviceControllerCompleteRemovalRequest)];
  fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

  auto req = builder.New<fuchsia_device_manager_DeviceControllerCompleteRemovalRequest>();
  ZX_ASSERT(req != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DeviceControllerCompleteRemovalOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
  return msg.Write(dev->channel()->get(), 0);
}

zx_status_t dh_send_create_composite_device(Devhost* dh, const Device* composite_dev,
                                            const CompositeDevice& composite,
                                            const uint64_t* component_local_ids, zx::channel rpc) {
  size_t components_size = composite.components_count() * sizeof(uint64_t);
  size_t name_size = composite.name().size();
  uint32_t wr_num_bytes = static_cast<uint32_t>(
      sizeof(fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequest) +
      FIDL_ALIGN(components_size) + FIDL_ALIGN(name_size));
  FIDL_ALIGNDECL char wr_bytes[wr_num_bytes];
  fidl::Builder builder(wr_bytes, wr_num_bytes);

  auto req = builder.New<fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequest>();
  uint64_t* components_data =
      builder.NewArray<uint64_t>(static_cast<uint32_t>(composite.components_count()));
  char* name_data = builder.NewArray<char>(static_cast<uint32_t>(name_size));
  ZX_ASSERT(req != nullptr && components_data != nullptr && name_data != nullptr);
  req->hdr.ordinal = fuchsia_device_manager_DevhostControllerCreateCompositeDeviceOrdinal;
  // TODO(teisenbe): Allocate and track txids
  req->hdr.txid = 1;

  req->rpc = FIDL_HANDLE_PRESENT;

  req->components.count = composite.components_count();
  req->components.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(components_data, component_local_ids, components_size);

  req->name.size = name_size;
  req->name.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  memcpy(name_data, composite.name().data(), name_size);

  req->local_device_id = composite_dev->local_id();

  zx_handle_t handles[1] = {rpc.release()};
  uint32_t num_handles = 1;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart(handles, num_handles, num_handles));
  return msg.Write(dh->hrpc(), 0);
}

}  // namespace devmgr
