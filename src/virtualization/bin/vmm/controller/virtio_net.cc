// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_net.h"

#include <lib/sys/cpp/service_directory.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace {

constexpr auto kComponentCollectionName = "virtio_net_devices";
constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_net_rs#meta/virtio_net_rs.cm";

}  // namespace

VirtioNet::VirtioNet(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio Net", phys_mem, VIRTIO_NET_F_MAC,
                            fit::bind_member(this, &VirtioNet::ConfigureQueue),
                            fit::bind_member(this, &VirtioNet::Ready)) {}

zx_status_t VirtioNet::Start(const zx::guest& guest,
                             const fuchsia::hardware::ethernet::MacAddress& mac_address,
                             bool enable_bridge, ::sys::ComponentContext* context,
                             async_dispatcher_t* dispatcher, size_t component_name_suffix) {
  std::string component_name = fxl::StringPrintf("virtio_net_%zu", component_name_suffix);
  zx_status_t status = CreateDynamicComponent(
      context, kComponentCollectionName, component_name.c_str(), kComponentUrl,
      [net = net_.NewRequest()](std::shared_ptr<sys::ServiceDirectory> services) mutable {
        return services->Connect(std::move(net));
      });
  if (status != ZX_OK) {
    return status;
  }

  {
    std::lock_guard<std::mutex> lock(device_config_.mutex);
    config_.status = VIRTIO_NET_S_LINK_UP;
    config_.max_virtqueue_pairs = 1;
    memcpy(config_.mac, mac_address.octets.data(), sizeof(config_.mac));
  }

  fuchsia::virtualization::hardware::StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }

  ::fuchsia::virtualization::hardware::VirtioNet_Start_Result result;
  status = net_->Start(std::move(start_info), mac_address, enable_bridge, &result);
  if (status != ZX_OK) {
    return status;
  }

  return result.is_err() ? result.err() : ZX_OK;
}

zx_status_t VirtioNet::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                      zx_gpaddr_t avail, zx_gpaddr_t used) {
  return net_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioNet::Ready(uint32_t negotiated_features) {
  return net_->Ready(negotiated_features);
}
