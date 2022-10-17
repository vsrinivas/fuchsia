// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/utils.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/syslog/cpp/macros.h>

#include "src/storage/fvm/format.h"

namespace fshost {

namespace {

constexpr uint64_t kDefaultVolumePercentage = 10;
constexpr uint64_t kDefaultVolumeSize = 24lu * 1024 * 1024;

}  // namespace

zx::result<uint64_t> ResizeVolume(
    fidl::UnownedClientEnd<fuchsia_hardware_block_volume::Volume> volume, uint64_t target_bytes,
    bool inside_zxcrypt) {
  // Free all the existing slices.
  uint64_t slice = 1;
  // The -1 here is because of zxcrypt; zxcrypt will offset all slices by 1 to account for its
  // header.  zxcrypt isn't present in all cases, but that won't matter since minfs shouldn't be
  // using a slice so high.
  while (slice < fvm::kMaxVSlices - 1) {
    auto query_result =
        fidl::WireCall(volume)->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(&slice, 1));
    if (query_result.status() != ZX_OK) {
      FX_PLOGS(ERROR, query_result.status())
          << "Unable to query slices (slice: " << slice << ", max: " << fvm::kMaxVSlices << ")";
      return zx::error(query_result.status());
    }

    if (query_result.value().status != ZX_OK) {
      FX_PLOGS(ERROR, query_result.value().status)
          << "Unable to query slices (slice: " << slice << ", max: " << fvm::kMaxVSlices << ")";
      return zx::error(query_result.value().status);
    }

    if (query_result.value().response_count == 0) {
      break;
    }

    for (uint64_t i = 0; i < query_result.value().response_count; ++i) {
      if (query_result.value().response[i].allocated) {
        auto shrink_result =
            fidl::WireCall(volume)->Shrink(slice, query_result.value().response[i].count);
        if (zx_status_t status =
                shrink_result.status() == ZX_OK ? shrink_result->status : shrink_result.status();
            status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Unable to shrink partition";
          return zx::error(status);
        }
      }
      slice += query_result.value().response[i].count;
    }
  }

  auto query_result = fidl::WireCall(volume)->GetVolumeInfo();
  if (query_result.status() != ZX_OK) {
    return zx::error(query_result.status());
  }
  if (query_result.value().status != ZX_OK) {
    return zx::error(query_result.value().status);
  }
  const uint64_t slice_size = query_result.value().manager->slice_size;

  // Count the first slice (which is already allocated to the volume) as available.
  const uint64_t slices_available = 1 + query_result.value().manager->slice_count -
                                    query_result.value().manager->assigned_slice_count;
  uint64_t slice_count = target_bytes / slice_size;
  if (slice_count == 0) {
    // If a size is not specified, limit the size of the data partition so as not to use up all
    // FVM's space (thus limiting blobfs growth).  10% or 24MiB (whichever is larger) should be
    // enough.
    const uint64_t default_slices = std::max<uint64_t>(
        query_result.value().manager->slice_count * kDefaultVolumePercentage / 100,
        kDefaultVolumeSize / slice_size);
    FX_LOGS(INFO) << "Using default size of " << default_slices * slice_size;
    slice_count = std::min(slices_available, default_slices);
  }
  if (slices_available < slice_count) {
    FX_LOGS(WARNING) << "Only " << slices_available << " slices available; some functionality "
                     << "may be missing.";
    slice_count = slices_available;
  }

  ZX_DEBUG_ASSERT(slice_count > 0);
  if (inside_zxcrypt) {
    // zxcrypt occupies an additional slice for its own metadata.
    --slice_count;
  }
  if (slice_count > 1) {
    auto extend_result = fidl::WireCall(volume)->Extend(
        1,
        slice_count - 1);  // -1 here because we get the first slice for free.
    if (zx_status_t status =
            extend_result.status() == ZX_OK ? extend_result->status : extend_result.status();
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Unable to extend partition (slice_count: " << slice_count << ")";
      return zx::error(status);
    }
  }

  return zx::ok(slice_count * slice_size);
}

zx::result<zx::channel> CloneNode(fidl::UnownedClientEnd<fuchsia_io::Node> node) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (endpoints.is_error())
    return endpoints.take_error();

  if (zx_status_t status =
          fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::Node>(node))
              ->Clone(fuchsia_io::wire::OpenFlags::kCloneSameRights, std::move(endpoints->server))
              .status();
      status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(endpoints->client).TakeChannel());
}

zx::result<std::string> GetDevicePath(fidl::UnownedClientEnd<fuchsia_device::Controller> device) {
  std::string device_path;
  if (auto result = fidl::WireCall(device)->GetTopologicalPath(); result.status() != ZX_OK) {
    return zx::error(result.status());
  } else if (result->is_error()) {
    return zx::error(result->error_value());
  } else {
    device_path = result->value()->path.get();
  }
  return zx::ok(device_path);
}

}  // namespace fshost
