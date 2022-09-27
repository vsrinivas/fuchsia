// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_balloon.h"

#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

namespace {

constexpr auto kComponentName = "virtio_balloon";
constexpr auto kComponentCollectionName = "virtio_balloon_devices";
constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_balloon#meta/virtio_balloon.cm";

}  // namespace

VirtioBalloon::VirtioBalloon(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio Balloon", phys_mem,
                            VIRTIO_BALLOON_F_STATS_VQ | VIRTIO_BALLOON_F_DEFLATE_ON_OOM,
                            fit::bind_member(this, &VirtioBalloon::ConfigureQueue),
                            fit::bind_member(this, &VirtioBalloon::Ready)) {}

zx_status_t VirtioBalloon::Start(const zx::guest& guest, ::sys::ComponentContext* context,
                                 async_dispatcher_t* dispatcher) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_virtualization_hardware::VirtioBalloon>();
  auto [client_end, server_end] = std::move(endpoints.value());
  fidl::InterfaceRequest<fuchsia::virtualization::hardware::VirtioBalloon> balloon_request(
      server_end.TakeChannel());
  balloon_.Bind(std::move(client_end), dispatcher, this);

  zx_status_t status =
      CreateDynamicComponent(context, kComponentCollectionName, kComponentName, kComponentUrl,
                             [balloon_request = std::move(balloon_request)](
                                 std::shared_ptr<sys::ServiceDirectory> services) mutable {
                               return services->Connect(std::move(balloon_request));
                             });
  if (status != ZX_OK) {
    return status;
  }

  fuchsia_virtualization_hardware::wire::StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }

  return balloon_.sync()->Start(std::move(start_info)).status();
}

void VirtioBalloon::ConnectToBalloonController(
    fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> endpoint) {
  bindings_.AddBinding(this, std::move(endpoint));
}

zx_status_t VirtioBalloon::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                          zx_gpaddr_t avail, zx_gpaddr_t used) {
  return balloon_.sync()->ConfigureQueue(queue, size, desc, avail, used).status();
}

zx_status_t VirtioBalloon::Ready(uint32_t negotiated_features) {
  zx_status_t status = balloon_.sync()->Ready(negotiated_features).status();
  return status;
}

void VirtioBalloon::GetBalloonSize(GetBalloonSizeCallback callback) {
  uint32_t current_num_pages, requested_num_pages;
  {
    std::lock_guard<std::mutex> lock(device_config_.mutex);
    requested_num_pages = config_.num_pages;
    current_num_pages = config_.actual;
  }
  callback(current_num_pages, requested_num_pages);
}

void VirtioBalloon::RequestNumPages(uint32_t num_pages) {
  {
    std::lock_guard<std::mutex> lock(device_config_.mutex);
    config_.num_pages = num_pages;
  }
  // Send a config change interrupt to the guest.
  zx_status_t status = Interrupt(VirtioQueue::SET_CONFIG | VirtioQueue::TRY_INTERRUPT);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to generate configuration interrupt " << status;
  }
}

void VirtioBalloon::on_fidl_error(fidl::UnbindInfo error) {
  FX_LOGS(ERROR) << "Connection to VirtioBalloon lost: " << error;
}

void VirtioBalloon::GetMemStats(GetMemStatsCallback callback) {
  balloon_->GetMemStats().Then([callback = std::move(callback)](auto& result) {
    if (result.ok()) {
      std::vector<::fuchsia::virtualization::MemStat> mem_stats;
      for (auto& el : result->mem_stats) {
        mem_stats.push_back({.tag = el.tag, .val = el.val});
      }
      callback(result->status, std::move(mem_stats));
    } else {
      FX_LOGS(ERROR) << "Failed to get memory stats status=" << result.status_string();
      callback(ZX_ERR_INTERNAL, {});
    }
  });
}
