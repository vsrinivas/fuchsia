// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_vsock.h"

#include <lib/sys/cpp/service_directory.h>

#include "src/virtualization/bin/vmm/controller/realm_utils.h"

namespace controller {

using ::fuchsia::virtualization::hardware::StartInfo;
using ::fuchsia::virtualization::hardware::VirtioVsock_Start_Result;

namespace {

// 5.10.3 Feature bits
//
// If no feature bit is set, only stream socket type is supported.
constexpr uint32_t kDeviceFeatures = 0;

constexpr const char* kComponentName = "virtio_vsock";
constexpr const char* kComponentCollectionName = "virtio_vsock_devices";
constexpr const char* kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_vsock#meta/virtio_vsock.cm";

// There is one device per guest, and one guest per host, so all guests will use the same CID.
constexpr uint64_t kGuestCid = fuchsia::virtualization::DEFAULT_GUEST_CID;

}  // namespace

VirtioVsock::VirtioVsock(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio Vsock", phys_mem, kDeviceFeatures,
                            fit::bind_member(this, &VirtioVsock::ConfigureQueue),
                            fit::bind_member(this, &VirtioVsock::Ready)) {
  std::lock_guard<std::mutex> lock(device_config_.mutex);
  config_.guest_cid = kGuestCid;
}

zx_status_t VirtioVsock::Start(const zx::guest& guest, fuchsia::component::RealmSyncPtr& realm,
                               async_dispatcher_t* dispatcher) {
  zx_status_t status =
      CreateDynamicComponent(realm, kComponentCollectionName, kComponentName, kComponentUrl,
                             [vsock = vsock_.NewRequest(), endpoint = endpoint_.NewRequest()](
                                 std::shared_ptr<sys::ServiceDirectory> services) mutable {
                               zx_status_t status = services->Connect(std::move(vsock));
                               if (status != ZX_OK) {
                                 return status;
                               }
                               return services->Connect(std::move(endpoint));
                             });
  if (status != ZX_OK) {
    return status;
  }

  StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }

  VirtioVsock_Start_Result result;
  status = vsock_->Start(std::move(start_info), kGuestCid, {}, &result);
  if (status != ZX_OK) {
    return status;
  }

  return result.is_err() ? result.err() : ZX_OK;
}

zx_status_t VirtioVsock::AddPublicService(sys::ComponentContext* context) {
  return context->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

zx_status_t VirtioVsock::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                        zx_gpaddr_t avail, zx_gpaddr_t used) {
  return vsock_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioVsock::Ready(uint32_t negotiated_features) {
  return vsock_->Ready(negotiated_features);
}

void VirtioVsock::Connect2(uint32_t guest_port, VirtioVsock::Connect2Callback callback) {
  endpoint_->Connect2(guest_port, std::move(callback));
}

void VirtioVsock::Listen(uint32_t port,
                         fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor> acceptor,
                         VirtioVsock::ListenCallback callback) {
  endpoint_->Listen(port, std::move(acceptor), std::move(callback));
}

}  // namespace controller
