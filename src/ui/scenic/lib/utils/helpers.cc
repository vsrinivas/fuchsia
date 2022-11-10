// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/helpers.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/handles/object_info.h"

#include <glm/gtc/constants.hpp>

using fuchsia::ui::composition::Orientation;

namespace utils {

fuchsia::ui::scenic::Present2Args CreatePresent2Args(zx_time_t requested_presentation_time,
                                                     std::vector<zx::event> acquire_fences,
                                                     std::vector<zx::event> release_fences,
                                                     zx_duration_t requested_prediction_span) {
  fuchsia::ui::scenic::Present2Args args;
  args.set_requested_presentation_time(requested_presentation_time);
  args.set_acquire_fences(std::move(acquire_fences));
  args.set_release_fences(std::move(release_fences));
  args.set_requested_prediction_span(requested_prediction_span);

  return args;
}

zx_koid_t ExtractKoid(const zx::object_base& object) {
  zx_info_handle_basic_t info{};
  if (object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

zx_koid_t ExtractRelatedKoid(const zx::object_base& object) {
  zx_info_handle_basic_t info{};
  if (object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.related_koid;
}

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  return ExtractKoid(view_ref.reference);
}

template <typename ZX_T>
static auto CopyZxHandle(const ZX_T& handle) -> ZX_T {
  ZX_T handle_copy;
  if (handle.duplicate(ZX_RIGHT_SAME_RIGHTS, &handle_copy) != ZX_OK) {
    FX_LOGS(ERROR) << "Copying zx object handle failed.";
    FX_DCHECK(false);
  }
  return handle_copy;
}

zx::event CopyEvent(const zx::event& event) { return CopyZxHandle(event); }

zx::eventpair CopyEventpair(const zx::eventpair& eventpair) { return CopyZxHandle(eventpair); }

std::vector<zx::event> CopyEventArray(const std::vector<zx::event>& events) {
  std::vector<zx::event> result;
  const size_t count = events.size();
  result.reserve(count);
  for (size_t i = 0; i < count; i++) {
    result.push_back(CopyEvent(events[i]));
  }
  return result;
}

bool IsEventSignalled(const zx::event& event, zx_signals_t signal) {
  zx_signals_t pending = 0u;
  event.wait_one(signal, zx::time(), &pending);
  return (pending & signal) != 0u;
}

zx::event CreateEvent() {
  zx::event event;
  FX_CHECK(zx::event::create(0, &event) == ZX_OK);
  return event;
}

std::vector<zx::event> CreateEventArray(size_t n) {
  std::vector<zx::event> events;
  events.reserve(n);
  for (size_t i = 0; i < n; i++) {
    events.push_back(CreateEvent());
  }
  return events;
}

std::vector<zx_koid_t> ExtractKoids(const std::vector<zx::event>& events) {
  std::vector<zx_koid_t> result;
  result.reserve(events.size());
  for (auto& evt : events) {
    zx_info_handle_basic_t info;
    zx_status_t status = evt.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    FX_DCHECK(status == ZX_OK);
    result.push_back(info.koid);
  }
  return result;
}

fuchsia::sysmem::AllocatorSyncPtr CreateSysmemAllocatorSyncPtr(
    const std::string& debug_name_suffix) {
  FX_CHECK(!debug_name_suffix.empty());
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  FX_DCHECK(status == ZX_OK);
  auto debug_name = fsl::GetCurrentProcessName() + " " + debug_name_suffix;
  constexpr size_t kMaxNameLength = 64;  // from fuchsia.sysmem/allocator.fidl
  FX_DCHECK(debug_name.length() <= kMaxNameLength)
      << "Sysmem client debug name exceeded max length of " << kMaxNameLength << " (\""
      << debug_name << "\")";

  sysmem_allocator->SetDebugClientInfo(std::move(debug_name), fsl::GetCurrentProcessKoid());
  return sysmem_allocator;
}

SysmemTokens CreateSysmemTokens(fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  FX_DCHECK(sysmem_allocator);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  FX_DCHECK(status == ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  FX_DCHECK(status == ZX_OK);
  status = local_token->Sync();
  FX_DCHECK(status == ZX_OK);

  return {std::move(local_token), std::move(dup_token)};
}

fuchsia::sysmem::BufferCollectionConstraints CreateDefaultConstraints(uint32_t buffer_count,
                                                                      uint32_t width,
                                                                      uint32_t height) {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageReadOften | fuchsia::sysmem::cpuUsageWriteOften;
  constraints.min_buffer_count = buffer_count;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] =
      fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;

  image_constraints.required_min_coded_width = width;
  image_constraints.required_min_coded_height = height;
  image_constraints.required_max_coded_width = width;
  image_constraints.required_max_coded_height = height;

  image_constraints.bytes_per_row_divisor = 4;

  return constraints;
}

bool RectFContainsPoint(const fuchsia::math::RectF& rect, float x, float y) {
  constexpr float kEpsilon = 1e-3f;
  return rect.x - kEpsilon <= x && x <= rect.x + rect.width + kEpsilon && rect.y - kEpsilon <= y &&
         y <= rect.y + rect.height + kEpsilon;
}

fuchsia::math::RectF ConvertRectToRectF(const fuchsia::math::Rect& rect) {
  return {.x = static_cast<float>(rect.x),
          .y = static_cast<float>(rect.y),
          .width = static_cast<float>(rect.width),
          .height = static_cast<float>(rect.height)};
}

// Prints in row-major order.
void PrettyPrintMat3(std::string name, const std::array<float, 9>& mat3) {
  FX_LOGS(INFO) << "\n"
                << name << ":\n"
                << mat3[0] << "," << mat3[3] << "," << mat3[6] << "\n"
                << mat3[1] << "," << mat3[4] << "," << mat3[7] << "\n"
                << mat3[2] << "," << mat3[5] << "," << mat3[8];
}

float GetOrientationAngle(fuchsia::ui::composition::Orientation orientation) {
  switch (orientation) {
    case Orientation::CCW_0_DEGREES:
      return 0.f;
    case Orientation::CCW_90_DEGREES:
      return -glm::half_pi<float>();
    case Orientation::CCW_180_DEGREES:
      return -glm::pi<float>();
    case Orientation::CCW_270_DEGREES:
      return -glm::three_over_two_pi<float>();
  }
}

}  // namespace utils
