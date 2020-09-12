// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_control/host_visible_heap.h"

#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/function.h>
#include <lib/zx/vmar.h>
#include <zircon/assert.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <fbl/auto_call.h>

#include "src/graphics/drivers/misc/goldfish_control/control_device.h"
#include "src/lib/fsl/handles/object_info.h"

namespace goldfish {

namespace {

static const char* kTag = "goldfish-host-visible-heap";

llcpp::fuchsia::sysmem2::HeapProperties GetHeapProperties() {
  auto coherency_domain_support =
      std::make_unique<llcpp::fuchsia::sysmem2::CoherencyDomainSupport>();
  *coherency_domain_support =
      llcpp::fuchsia::sysmem2::CoherencyDomainSupport::Builder(
          std::make_unique<llcpp::fuchsia::sysmem2::CoherencyDomainSupport::Frame>())
          .set_cpu_supported(std::make_unique<bool>(true))
          .set_ram_supported(std::make_unique<bool>(true))
          .set_inaccessible_supported(std::make_unique<bool>(false))
          .build();

  return llcpp::fuchsia::sysmem2::HeapProperties::Builder(
             std::make_unique<llcpp::fuchsia::sysmem2::HeapProperties::Frame>())
      .set_coherency_domain_support(std::move(coherency_domain_support))
      // Allocated VMOs are not directly writeable since they are physical
      // VMOs on MMIO; Also, contents of VMOs allocated by this Heap are only
      // valid after |CreateColorBuffer()| render control call. Thus it doesn't
      // work for sysmem to clear the VMO contents, instead we do map-and-clear
      // in the end of |CreateResource()|.
      .set_need_clear(std::make_unique<bool>(false))
      .build();
}

}  // namespace

HostVisibleHeap::Block::Block(zx::vmo vmo, uint64_t paddr, fit::closure deallocate_callback,
                              async_dispatcher_t* dispatcher)
    : vmo(std::move(vmo)),
      paddr(paddr),
      wait_deallocate(this->vmo.get(), ZX_VMO_ZERO_CHILDREN, 0u,
                      [deallocate_callback = std::move(deallocate_callback)](
                          async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                          const zx_packet_signal_t* signal) { deallocate_callback(); }) {
  wait_deallocate.Begin(dispatcher);
}

// static
std::unique_ptr<HostVisibleHeap> HostVisibleHeap::Create(Control* control) {
  // Using `new` to access a non-public constructor.
  return std::unique_ptr<HostVisibleHeap>(new HostVisibleHeap(control));
}

HostVisibleHeap::HostVisibleHeap(Control* control) : Heap(control, kTag) {}

HostVisibleHeap::~HostVisibleHeap() = default;

void HostVisibleHeap::AllocateVmo(uint64_t size, AllocateVmoCompleter::Sync completer) {
  TRACE_DURATION("gfx", "HostVisibleHeap::AllocateVmo", "size", size);

  auto result = control()->address_space_child()->AllocateBlock(size);
  if (!result.ok()) {
    zxlogf(ERROR, "[%s] AllocateBlock FIDL call failed: status %d", kTag, result.status());
    completer.Reply(result.status(), zx::vmo{});
    return;
  }
  if (result.value().res != ZX_OK) {
    zxlogf(ERROR, "[%s] AllocateBlock failed: res %d", kTag, result.value().res);
    completer.Reply(result.value().res, zx::vmo{});
    return;
  }

  // We need to clean up the allocated block if |zx_vmo_create_child| or
  // |fsl::GetKoid| fails, which could happen before we create and bind the
  // |DeallocateBlock()| wait in |Block|.
  auto cleanup_block = fbl::MakeAutoCall([this, paddr = result.value().paddr] {
    auto result = control()->address_space_child()->DeallocateBlock(paddr);
    if (!result.ok()) {
      zxlogf(ERROR, "[%s] DeallocateBlock FIDL call failed: status %d", kTag, result.status());
    } else if (result.value().res != ZX_OK) {
      zxlogf(ERROR, "[%s] DeallocateBlock failed: res %d", kTag, result.value().res);
    }
  });

  zx::vmo vmo = std::move(result.value().vmo);
  zx::vmo child;
  zx_status_t status = vmo.create_child(ZX_VMO_CHILD_SLICE, /*offset=*/0u, size, &child);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx_vmo_create_child failed: %d", kTag, status);
    completer.Close(status);
    return;
  }

  zx_handle_t child_handle = child.get();
  zx_koid_t child_koid = fsl::GetKoid(child_handle);
  if (child_koid == ZX_KOID_INVALID) {
    zxlogf(ERROR, "[%s] fsl::GetKoid failed: child_handle %u", kTag, child_handle);
    completer.Close(ZX_ERR_BAD_HANDLE);
    return;
  }

  blocks_.try_emplace(
      child_koid, std::move(vmo), result.value().paddr,
      [this, child_koid] { DeallocateVmo(child_koid); }, loop()->dispatcher());

  cleanup_block.cancel();
  completer.Reply(ZX_OK, std::move(child));
}

void HostVisibleHeap::DeallocateVmo(zx_koid_t koid) {
  TRACE_DURATION("gfx", "HostVisibleHeap::DeallocateVmo");

  ZX_DEBUG_ASSERT(koid != ZX_KOID_INVALID);
  ZX_DEBUG_ASSERT(blocks_.find(koid) != blocks_.end());

  uint64_t paddr = blocks_.at(koid).paddr;
  blocks_.erase(koid);

  auto result = control()->address_space_child()->DeallocateBlock(paddr);
  if (!result.ok()) {
    zxlogf(ERROR, "[%s] DeallocateBlock FIDL call error: status %d", kTag, result.status());
  } else if (result.value().res != ZX_OK) {
    zxlogf(ERROR, "[%s] DeallocateBlock failed: res %d", kTag, result.value().res);
  }
}

void HostVisibleHeap::CreateResource(::zx::vmo vmo,
                                     llcpp::fuchsia::sysmem2::SingleBufferSettings buffer_settings,
                                     CreateResourceCompleter::Sync completer) {
  using llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType;
  using llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params;
  using llcpp::fuchsia::sysmem2::PixelFormatType;

  ZX_DEBUG_ASSERT(vmo.is_valid());

  // Argument validation.

  // TODO(59805): Support data buffers.
  if (!buffer_settings.has_image_format_constraints()) {
    zxlogf(ERROR, "[%s] Currently only image allocation is supported", kTag);
    completer.Reply(ZX_ERR_NOT_SUPPORTED, 0u);
    return;
  }

  const auto& image_constraints = buffer_settings.image_format_constraints();
  if (!image_constraints.has_pixel_format() || !image_constraints.pixel_format().has_type() ||
      !image_constraints.has_min_coded_width() || !image_constraints.has_min_coded_height()) {
    zxlogf(ERROR, "[%s] image_constraints missing arguments: pixel_format %d width %d height %d",
           kTag, image_constraints.has_pixel_format(), image_constraints.has_min_coded_width(),
           image_constraints.has_min_coded_height());
    completer.Reply(ZX_ERR_INVALID_ARGS, 0u);
    return;
  }

  // TODO(59804): Support other pixel formats.
  const auto& pixel_format_type = image_constraints.pixel_format().type();
  ColorBufferFormatType color_buffer_format;
  switch (pixel_format_type) {
    case PixelFormatType::BGRA32:
      color_buffer_format = ColorBufferFormatType::BGRA;
      break;
    case PixelFormatType::R8G8B8A8:
      color_buffer_format = ColorBufferFormatType::RGBA;
      break;
    default:
      zxlogf(ERROR, "[%s] pixel_format_type unsupported: type %u", kTag, pixel_format_type);
      completer.Reply(ZX_ERR_NOT_SUPPORTED, 0u);
      return;
  }

  TRACE_DURATION("gfx", "HostVisibleHeap::CreateResource", "width",
                 image_constraints.min_coded_width(), "height",
                 image_constraints.min_coded_height(), "format",
                 static_cast<uint32_t>(pixel_format_type));

  zx_info_vmo_t vmo_info;
  zx_status_t status = vmo.get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx_object_get_info failed: status %d", kTag, status);
    completer.Close(status);
    return;
  }

  // The |vmo| passed in to this function is the child of VMO returned by
  // |AllocateVmo()| above. So the key should be the parent koid of |vmo|.
  zx_koid_t vmo_parent_koid = vmo_info.parent_koid;
  if (blocks_.find(vmo_parent_koid) == blocks_.end()) {
    zxlogf(ERROR, "[%s] Cannot find parent VMO koid in heap: parent_koid %lu", kTag,
           vmo_parent_koid);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint64_t id = control()->RegisterBufferHandle(vmo);
  if (id == ZX_KOID_INVALID) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // If the following method fails, we need to free the color buffer handle so
  // that there is no handle/resource leakage.
  auto cleanup_handle = fbl::MakeAutoCall([this, id] { control()->FreeBufferHandle(id); });

  uint32_t width = image_constraints.min_coded_width();
  uint32_t height = image_constraints.min_coded_height();
  uint64_t paddr = blocks_.at(vmo_parent_koid).paddr;
  auto create_params =
      CreateColorBuffer2Params::Builder(std::make_unique<CreateColorBuffer2Params::Frame>())
          .set_width(std::make_unique<uint32_t>(width))
          .set_height(std::make_unique<uint32_t>(height))
          .set_memory_property(std::make_unique<uint32_t>(
              llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE))
          .set_physical_address(std::make_unique<uint64_t>(paddr))
          .set_format(std::make_unique<ColorBufferFormatType>(color_buffer_format))
          .build();

  zx::vmo vmo_dup;
  status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx_handle_duplicate failed: %d", kTag, status);
    completer.Close(status);
    return;
  }

  // Create actual color buffer and map guest physical address |paddr| to
  // host memory.
  auto result = control()->CreateColorBuffer2(std::move(vmo_dup), std::move(create_params));
  if (result.is_error()) {
    zxlogf(ERROR, "[%s] CreateColorBuffer error: status %d", kTag, status);
    completer.Close(result.error());
    return;
  }
  if (result.value().res != ZX_OK) {
    zxlogf(ERROR, "[%s] CreateColorBuffer2 failed: res = %d", kTag, result.value().res);
    completer.Reply(result.value().res, 0u);
    return;
  }

  // Host visible ColorBuffer should have page offset of zero, otherwise
  // part of the host page can be leaked to guest.
  ZX_DEBUG_ASSERT(result.value().hw_address_page_offset == 0u);

  // Heap should fill VMO with zeroes before returning it to clients.
  // Since VMOs allocated by address space device are physical VMOs not
  // supporting zx_vmo_write, we map it and fill the mapped memory address
  // with zero.
  uint64_t vmo_size;
  status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx_vmo_get_size failed: %d", kTag, status);
    completer.Close(status);
    return;
  }

  zx_paddr_t addr;
  status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, /*vmar_offset=*/0u, vmo,
                                      /*vmo_offset=*/0u, vmo_size, &addr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx_vmar_map failed: %d", kTag, status);
    completer.Close(status);
    return;
  }

  memset(reinterpret_cast<uint8_t*>(addr), 0u, vmo_size);

  status = zx::vmar::root_self()->unmap(addr, vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx_vmar_unmap failed: %d", kTag, status);
    completer.Close(status);
    return;
  }

  // Everything is done, now we can cancel the cleanup auto call.
  cleanup_handle.cancel();
  completer.Reply(ZX_OK, id);
}

void HostVisibleHeap::DestroyResource(uint64_t id, DestroyResourceCompleter::Sync completer) {
  // This destroys the color buffer associated with |id| and frees the color
  // buffer handle |id|.
  control()->FreeBufferHandle(id);
  completer.Reply();
}

void HostVisibleHeap::Bind(zx::channel server_request) {
  BindWithHeapProperties(std::move(server_request), GetHeapProperties());
}

}  // namespace goldfish
