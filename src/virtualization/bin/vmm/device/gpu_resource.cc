// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/gpu_resource.h"

#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/compiler.h>

#include <cstddef>

#include <safemath/checked_math.h>

namespace {

using safemath::CheckAdd;
using safemath::CheckedNumeric;
using safemath::CheckMul;

// Maximum size buffer we are willing to allocate for a user.
constexpr size_t kMaxBufferSize = 256ul * 1024 * 1024;  // 256 MiB

// Shorthand for creating a safemath::CheckedNumeric.
template <typename T>
auto Checked(T n) -> auto {
  return safemath::MakeCheckedNum(n);
}

// Return a subspan of the given fbl::Array.
//
// Aborts if the input is out of range.
cpp20::span<std::byte> ArraySubspan(fbl::Array<std::byte>& source, size_t offset, size_t size) {
  size_t end;
  FX_CHECK(!add_overflow(offset, size, &end) && end <= source.size())
      << "Specified range is out of bounds";
  return cpp20::span<std::byte>(source.data() + offset, size);
}

// Copy the bytes in the span `src` to the span `dest`.
//
// Aborts if the size of `src` exceeds the size of `dest`.
void CopyBytes(cpp20::span<std::byte> dest, cpp20::span<const std::byte> src) {
  FX_CHECK(dest.size() >= src.size()) << "Destination memory range smaller than source range";
  memcpy(dest.data(), src.data(), src.size());
}

// Zero the bytes in the given span.
void ZeroBytes(cpp20::span<std::byte> dest) { memset(dest.data(), 0, dest.size()); }

}  // namespace

zx::status<GpuResource> GpuResource::Create(const PhysMem& phys_mem, uint32_t format,
                                            uint32_t width, uint32_t height) {
  // Ensure the created buffer is not too large.
  uint64_t buffer_size;
  if (!CheckMul(width, height, kPixelSizeInBytes).AssignIfValid(&buffer_size) ||
      buffer_size > kMaxBufferSize) {
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  return zx::success(
      GpuResource(phys_mem, format, width, height, fbl::MakeArray<std::byte>(buffer_size)));
}

GpuResource::GpuResource(const PhysMem& phys_mem, uint32_t format, uint32_t width, uint32_t height,
                         fbl::Array<std::byte> host_backing)
    : phys_mem_(&phys_mem),
      width_(width),
      height_(height),
      host_backing_(std::move(host_backing)) {}

void GpuResource::AttachBacking(const virtio_gpu_mem_entry_t* mem_entries, uint32_t num_entries) {
  // NOTE: it is valid for driver to leave regions of the image without backing,
  // so long as a transfer is never requested for them.
  guest_backing_.resize(num_entries);
  for (uint32_t i = 0; i < num_entries; ++i) {
    guest_backing_[i] = {
        .addr = mem_entries[i].addr,
        .len = mem_entries[i].length,
    };
  }
}

void GpuResource::DetachBacking() { guest_backing_.clear(); }

virtio_gpu_ctrl_type GpuResource::TransferToHost2d(const virtio_gpu_rect_t& rect, uint64_t off) {
  // Ensure that the requested coordinates are in range.
  uint64_t x_end, y_end;
  if (!CheckAdd(rect.x, rect.width).AssignIfValid(&x_end)) {
    return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
  }
  if (!CheckAdd(rect.y, rect.height).AssignIfValid(&y_end)) {
    return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
  }
  if (x_end > width_ || y_end > height_) {
    return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
  }

  // Ensure the requested offset is valid.
  CheckedNumeric expected_offset =
      ((Checked(rect.y) * Checked(width_)) + Checked(rect.x)) * kPixelSizeInBytes;
  if (!expected_offset.IsValid() || expected_offset.ValueOrDie() != off) {
    return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
  }

  const size_t rect_row_bytes = static_cast<uint64_t>(rect.width) * kPixelSizeInBytes;
  const size_t image_row_bytes = static_cast<uint64_t>(width_) * kPixelSizeInBytes;
  size_t transfer_bytes_remaining = rect_row_bytes * rect.height;
  size_t rect_row_bytes_remaining = rect_row_bytes;
  uint64_t entry_off = 0;
  for (const auto& entry : guest_backing_) {
    if (transfer_bytes_remaining == 0) {
      break;
    }
    while (entry_off + entry.len > off && transfer_bytes_remaining > 0) {
      // Current entry covers requested content.
      size_t copy_size = std::min((entry_off + entry.len) - off, transfer_bytes_remaining);
      uint64_t off_next = off + copy_size;

      // If the copy rect width does not match the resource width, additional
      // logic is required to skip data between rows.
      if (rect.width != width_) {
        if (rect_row_bytes_remaining <= copy_size) {
          // Clamp the copy size to the rect row size.
          copy_size = rect_row_bytes_remaining;
          // Set the next offset to the start of the next image row.
          off_next = (off + image_row_bytes + rect_row_bytes_remaining) - rect_row_bytes;
          // Reset remaining bytes in the rect row.
          rect_row_bytes_remaining = rect_row_bytes;
        } else {
          rect_row_bytes_remaining -= copy_size;
        }
      }

      zx_vaddr_t src_vaddr = entry.addr + off - entry_off;
      CopyBytes(/*dest=*/ArraySubspan(host_backing_, /*offset=*/off, /*size=*/copy_size),
                phys_mem_->span(src_vaddr, copy_size));
      transfer_bytes_remaining -= copy_size;
      off = off_next;
    }
    entry_off += entry.len;
  }
  if (transfer_bytes_remaining > 0) {
    FX_LOGS(WARNING) << "Transfer requested from unbacked pages";
    ZeroBytes(ArraySubspan(host_backing_, off, transfer_bytes_remaining));
    return VIRTIO_GPU_RESP_ERR_UNSPEC;
  }
  return VIRTIO_GPU_RESP_OK_NODATA;
}
