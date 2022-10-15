// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_control/host_visible_heap.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>
#include <lib/zx/vmar.h>
#include <zircon/assert.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/algorithm.h>

#include "src/graphics/drivers/misc/goldfish_control/control_device.h"
#include "src/lib/fsl/handles/object_info.h"

namespace goldfish {

namespace {

static const char* kTag = "goldfish-host-visible-heap";

fuchsia_sysmem2::wire::HeapProperties GetHeapProperties(fidl::AnyArena& allocator) {
  fuchsia_sysmem2::wire::CoherencyDomainSupport coherency_domain_support(allocator);
  coherency_domain_support.set_cpu_supported(true)
      .set_ram_supported(true)
      .set_inaccessible_supported(false);

  fuchsia_sysmem2::wire::HeapProperties heap_properties(allocator);
  heap_properties
      .set_coherency_domain_support(allocator, std::move(coherency_domain_support))
      // Allocated VMOs are not directly writeable since they are physical
      // VMOs on MMIO; Also, contents of VMOs allocated by this Heap are only
      // valid after |CreateColorBuffer()| render control call. Thus it doesn't
      // work for sysmem to clear the VMO contents, instead we do map-and-clear
      // in the end of |CreateResource()|.
      .set_need_clear(false);
  return heap_properties;
}

zx_status_t CheckSingleBufferSettings(
    const fuchsia_sysmem2::wire::SingleBufferSettings& single_buffer_settings) {
  bool has_image_format_constraints = single_buffer_settings.has_image_format_constraints();
  bool has_buffer_settings = single_buffer_settings.has_buffer_settings();

  if (!has_buffer_settings && !has_image_format_constraints) {
    zxlogf(ERROR,
           "[%s][%s] Both buffer_settings and image_format_constraints "
           "are missing, SingleBufferSettings is invalid.",
           kTag, __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  if (has_image_format_constraints) {
    const auto& image_constraints = single_buffer_settings.image_format_constraints();
    if (!image_constraints.has_pixel_format() || !image_constraints.pixel_format().has_type() ||
        !image_constraints.has_min_coded_width() || !image_constraints.has_min_coded_height()) {
      zxlogf(ERROR,
             "[%s][%s] image_constraints missing arguments: pixel_format %d width %d height %d",
             kTag, __func__, image_constraints.has_pixel_format(),
             image_constraints.has_min_coded_width(), image_constraints.has_min_coded_height());
      return ZX_ERR_INVALID_ARGS;
    }
  }

  if (has_buffer_settings) {
    const auto& buffer_settings = single_buffer_settings.buffer_settings();
    if (!buffer_settings.has_size_bytes()) {
      zxlogf(ERROR, "[%s][%s] buffer_settings missing arguments: size %d", kTag, __func__,
             buffer_settings.has_size_bytes());
      return ZX_ERR_INVALID_ARGS;
    }
  }

  return ZX_OK;
}

fpromise::result<fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params, zx_status_t>
GetCreateColorBuffer2Params(fidl::AnyArena& allocator,
                            const fuchsia_sysmem2::wire::SingleBufferSettings& buffer_settings,
                            uint64_t paddr) {
  using fuchsia_hardware_goldfish::wire::ColorBufferFormatType;
  using fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params;
  using fuchsia_sysmem2::wire::PixelFormatType;

  ZX_DEBUG_ASSERT(buffer_settings.has_image_format_constraints());
  const auto& image_constraints = buffer_settings.image_format_constraints();

  ZX_DEBUG_ASSERT(
      image_constraints.has_pixel_format() && image_constraints.pixel_format().has_type() &&
      image_constraints.has_min_coded_width() && image_constraints.has_min_coded_height());

  // TODO(fxbug.dev/59804): Support other pixel formats.
  const auto& pixel_format_type = image_constraints.pixel_format().type();
  ColorBufferFormatType color_buffer_format;
  switch (pixel_format_type) {
    case PixelFormatType::kBgra32:
      color_buffer_format = ColorBufferFormatType::kBgra;
      break;
    case PixelFormatType::kR8G8B8A8:
      color_buffer_format = ColorBufferFormatType::kRgba;
      break;
    case PixelFormatType::kR8:
      color_buffer_format = ColorBufferFormatType::kLuminance;
      break;
    case PixelFormatType::kR8G8:
      color_buffer_format = ColorBufferFormatType::kRg;
      break;
    default:
      zxlogf(ERROR, "[%s][%s] pixel_format_type unsupported: type %u", __func__, kTag,
             static_cast<uint32_t>(pixel_format_type));
      return fpromise::error(ZX_ERR_NOT_SUPPORTED);
  }

  uint32_t width = image_constraints.min_coded_width();
  if (image_constraints.has_required_max_coded_width()) {
    width = std::max(width, image_constraints.required_max_coded_width());
  }
  width = fbl::round_up(width, image_constraints.has_coded_width_divisor()
                                   ? image_constraints.coded_width_divisor()
                                   : 1);

  uint32_t height = image_constraints.min_coded_height();
  if (image_constraints.has_required_max_coded_height()) {
    height = std::max(height, image_constraints.required_max_coded_height());
  }
  height = fbl::round_up(height, image_constraints.has_coded_height_divisor()
                                     ? image_constraints.coded_height_divisor()
                                     : 1);

  CreateColorBuffer2Params buffer2_params(allocator);
  buffer2_params.set_width(width)
      .set_height(height)
      .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible)
      .set_physical_address(allocator, paddr)
      .set_format(color_buffer_format);
  return fpromise::ok(std::move(buffer2_params));
}

fuchsia_hardware_goldfish::wire::CreateBuffer2Params GetCreateBuffer2Params(
    fidl::AnyArena& allocator,
    const fuchsia_sysmem2::wire::SingleBufferSettings& single_buffer_settings, uint64_t paddr) {
  using fuchsia_hardware_goldfish::wire::CreateBuffer2Params;
  using fuchsia_sysmem2::wire::PixelFormatType;

  ZX_DEBUG_ASSERT(single_buffer_settings.has_buffer_settings());

  const auto& buffer_settings = single_buffer_settings.buffer_settings();

  ZX_DEBUG_ASSERT(buffer_settings.has_size_bytes());
  uint64_t size_bytes = buffer_settings.size_bytes();
  CreateBuffer2Params buffer2_params(allocator);
  buffer2_params.set_size(allocator, size_bytes)
      .set_memory_property(fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible)
      .set_physical_address(allocator, paddr);
  return buffer2_params;
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

void HostVisibleHeap::AllocateVmo(AllocateVmoRequestView request,
                                  AllocateVmoCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "HostVisibleHeap::AllocateVmo", "size", request->size);

  auto result = control()->address_space_child()->AllocateBlock(request->size);
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
  auto cleanup_block = fit::defer([this, paddr = result.value().paddr] {
    auto result = control()->address_space_child()->DeallocateBlock(paddr);
    if (!result.ok()) {
      zxlogf(ERROR, "[%s] DeallocateBlock FIDL call failed: status %d", kTag, result.status());
    } else if (result.value().res != ZX_OK) {
      zxlogf(ERROR, "[%s] DeallocateBlock failed: res %d", kTag, result.value().res);
    }
  });

  zx::vmo vmo = std::move(result.value().vmo);
  zx::vmo child;
  zx_status_t status = vmo.create_child(ZX_VMO_CHILD_SLICE, /*offset=*/0u, request->size, &child);
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

void HostVisibleHeap::CreateResource(CreateResourceRequestView request,
                                     CreateResourceCompleter::Sync& completer) {
  using fuchsia_hardware_goldfish::wire::ColorBufferFormatType;
  using fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params;
  using fuchsia_sysmem2::wire::PixelFormatType;

  ZX_DEBUG_ASSERT(request->vmo.is_valid());

  zx_status_t status = CheckSingleBufferSettings(request->buffer_settings);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Invalid single buffer settings", kTag);
    completer.Reply(status, 0u);
    return;
  }

  bool is_image = request->buffer_settings.has_image_format_constraints();
  TRACE_DURATION(
      "gfx", "HostVisibleHeap::CreateResource", "type", is_image ? "image" : "buffer",
      "image:width",
      is_image ? request->buffer_settings.image_format_constraints().min_coded_width() : 0,
      "image:height",
      is_image ? request->buffer_settings.image_format_constraints().min_coded_height() : 0,
      "image:format",
      is_image ? static_cast<uint32_t>(
                     request->buffer_settings.image_format_constraints().pixel_format().type())
               : 0,
      "buffer:size", is_image ? 0 : request->buffer_settings.buffer_settings().size_bytes());

  // Get |paddr| of the |Block| to use in Buffer create params.
  zx_info_vmo_t vmo_info;
  status = request->vmo.get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr);
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
  uint64_t paddr = blocks_.at(vmo_parent_koid).paddr;

  // Duplicate VMO to create ColorBuffer/Buffer.
  zx::vmo vmo_dup;
  status = request->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx_handle_duplicate failed: %d", kTag, status);
    completer.Close(status);
    return;
  }

  // Register buffer handle for VMO.
  uint64_t id = control()->RegisterBufferHandle(request->vmo);
  if (id == ZX_KOID_INVALID) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // If the following part fails, we need to free the ColorBuffer/Buffer
  // handle so that there is no handle/resource leakage.
  auto cleanup_handle = fit::defer([this, id] { control()->FreeBufferHandle(id); });

  if (is_image) {
    fidl::Arena allocator;
    // ColorBuffer creation.
    auto create_params = GetCreateColorBuffer2Params(allocator, request->buffer_settings, paddr);
    if (create_params.is_error()) {
      completer.Reply(create_params.error(), 0u);
      return;
    }

    // Create actual ColorBuffer and map physical address |paddr| to
    // address of the ColorBuffer's host memory.
    auto result = control()->CreateColorBuffer2(std::move(vmo_dup), create_params.take_value());
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
    // part of the page mapped from address space device not used for the
    // ColorBuffer can be leaked.
    ZX_DEBUG_ASSERT(result.value().hw_address_page_offset == 0u);
  } else {
    fidl::Arena allocator;
    // Data buffer creation.
    auto create_params = GetCreateBuffer2Params(allocator, request->buffer_settings, paddr);

    // Create actual data buffer and map physical address |paddr| to
    // address of the buffer's host memory.
    auto result = control()->CreateBuffer2(allocator, std::move(vmo_dup), std::move(create_params));
    if (result.is_error()) {
      zxlogf(ERROR, "[%s] CreateBuffer2 error: status %d", kTag, status);
      completer.Close(result.error());
      return;
    }
    if (result.value().is_err()) {
      zxlogf(ERROR, "[%s] CreateBuffer2 failed: res = %d", kTag, result.value().err());
      completer.Reply(result.value().err(), 0u);
      return;
    }

    // Host visible Buffer should have page offset of zero, otherwise
    // part of the page mapped from address space device not used for
    // the buffer can be leaked.
    ZX_DEBUG_ASSERT(result.value().response().hw_address_page_offset == 0u);
  }

  // Heap should fill VMO with zeroes before returning it to clients.
  // Since VMOs allocated by address space device are physical VMOs not
  // supporting zx_vmo_write, we map it and fill the mapped memory address
  // with zero.
  uint64_t vmo_size;
  status = request->vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx_vmo_get_size failed: %d", kTag, status);
    completer.Close(status);
    return;
  }

  zx_paddr_t addr;
  status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, /*vmar_offset=*/0u,
                                      request->vmo,
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

void HostVisibleHeap::DestroyResource(DestroyResourceRequestView request,
                                      DestroyResourceCompleter::Sync& completer) {
  // This destroys the color buffer associated with |id| and frees the color
  // buffer handle |id|.
  control()->FreeBufferHandle(request->id);
  completer.Reply();
}

void HostVisibleHeap::Bind(zx::channel server_request) {
  auto allocator = std::make_unique<fidl::Arena<512>>();
  fuchsia_sysmem2::wire::HeapProperties heap_properties = GetHeapProperties(*allocator.get());
  BindWithHeapProperties(std::move(server_request), std::move(allocator),
                         std::move(heap_properties));
}

}  // namespace goldfish
