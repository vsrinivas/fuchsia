// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_net.h"

#include <lib/sys/cpp/service_directory.h>

static constexpr char kVirtioNetUrl[] = "fuchsia-pkg://fuchsia.com/virtio_net#meta/virtio_net.cmx";

VirtioNet::VirtioNet(const PhysMem& phys_mem)
    : VirtioComponentDevice(phys_mem, VIRTIO_NET_F_MAC,
                            fit::bind_member(this, &VirtioNet::ConfigureQueue),
                            fit::bind_member(this, &VirtioNet::Ready)) {}

zx_status_t VirtioNet::Start(const zx::guest& guest,
                             const fuchsia::hardware::ethernet::MacAddress& mac_address,
                             fuchsia::sys::Launcher* launcher, async_dispatcher_t* dispatcher) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kVirtioNetUrl;
  auto services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services->Connect(net_.NewRequest());

  fuchsia::virtualization::hardware::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  status = net_->Start(std::move(start_info), mac_address);
  if (status != ZX_OK) {
    return status;
  }

  std::lock_guard<std::mutex> lock(device_config_.mutex);
  config_.status = VIRTIO_NET_S_LINK_UP;
  config_.max_virtqueue_pairs = 1;
  memcpy(config_.mac, mac_address.octets.data(), sizeof(config_.mac));

  return ZX_OK;
}

zx_status_t VirtioNet::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                      zx_gpaddr_t avail, zx_gpaddr_t used) {
  return net_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioNet::Ready(uint32_t negotiated_features) {
  return net_->Ready(negotiated_features);
}
