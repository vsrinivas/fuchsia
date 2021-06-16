// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/balloon.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <iostream>

#include <virtio/balloon.h>

#include "src/virtualization/bin/guest/services.h"

namespace {

zx::status<fuchsia::virtualization::BalloonControllerSyncPtr> ConnectToBalloon(
    sys::ComponentContext* context, uint32_t env_id, uint32_t cid) {
  zx::status<fuchsia::virtualization::RealmSyncPtr> env_ptr = ConnectToEnvironment(context, env_id);
  if (env_ptr.is_error()) {
    return env_ptr.take_error();
  }

  fuchsia::virtualization::BalloonControllerSyncPtr balloon_controller;
  zx_status_t status = env_ptr->ConnectToBalloon(cid, balloon_controller.NewRequest());
  if (status != ZX_OK) {
    std::cout << "Could not connect to balloon controller: " << zx_status_get_string(status)
              << ".\n";
    return zx::error(status);
  }

  return zx::ok(std::move(balloon_controller));
}

}  // namespace

zx_status_t handle_balloon(uint32_t env_id, uint32_t cid, uint32_t num_pages,
                           sys::ComponentContext* context) {
  zx::status<fuchsia::virtualization::BalloonControllerSyncPtr> balloon_controller =
      ConnectToBalloon(context, env_id, cid);
  if (balloon_controller.is_error()) {
    return balloon_controller.error_value();
  }

  zx_status_t status = balloon_controller->RequestNumPages(num_pages);
  if (status != ZX_OK) {
    std::cout << "Could not query balloon controller:" << zx_status_get_string(status) << ".\n";
    return status;
  }

  std::cout << "Resizing the memory balloon to " << num_pages << " pages\n";
  return ZX_OK;
}

static const char* tag_name(uint16_t tag) {
  switch (tag) {
    case VIRTIO_BALLOON_S_SWAP_IN:
      return "swap-in:             ";
    case VIRTIO_BALLOON_S_SWAP_OUT:
      return "swap-out:            ";
    case VIRTIO_BALLOON_S_MAJFLT:
      return "major-faults:        ";
    case VIRTIO_BALLOON_S_MINFLT:
      return "minor-faults:        ";
    case VIRTIO_BALLOON_S_MEMFREE:
      return "free-memory:         ";
    case VIRTIO_BALLOON_S_MEMTOT:
      return "total-memory:        ";
    case VIRTIO_BALLOON_S_AVAIL:
      return "available-memory:    ";
    case VIRTIO_BALLOON_S_CACHES:
      return "disk-caches:         ";
    case VIRTIO_BALLOON_S_HTLB_PGALLOC:
      return "hugetlb-allocations: ";
    case VIRTIO_BALLOON_S_HTLB_PGFAIL:
      return "hugetlb-failures:    ";
    default:
      return "unknown:             ";
  }
}

zx_status_t handle_balloon_stats(uint32_t env_id, uint32_t cid, sys::ComponentContext* context) {
  // Connect to balloon controller.
  zx::status<fuchsia::virtualization::BalloonControllerSyncPtr> balloon_controller =
      ConnectToBalloon(context, env_id, cid);
  if (balloon_controller.is_error()) {
    return balloon_controller.error_value();
  }

  // Fetch the statistics.
  zx_status_t status;
  fidl::VectorPtr<fuchsia::virtualization::MemStat> mem_stats;
  balloon_controller->GetMemStats(&status, &mem_stats);
  if (status != ZX_OK) {
    std::cerr << "Failed to get memory statistics " << status << '\n';
    return status;
  }
  for (auto& mem_stat : *mem_stats) {
    std::cout << tag_name(mem_stat.tag) << mem_stat.val << '\n';
  }

  return ZX_OK;
}
