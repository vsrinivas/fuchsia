// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_
#define SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/zx/event.h>

namespace utils {

using SysmemTokens = struct {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
};

constexpr std::array<float, 2> kDefaultPixelScale = {1.f, 1.f};

// Helper for creating a Present2Args fidl struct.
fuchsia::ui::scenic::Present2Args CreatePresent2Args(zx_time_t requested_presentation_time,
                                                     std::vector<zx::event> acquire_fences,
                                                     std::vector<zx::event> release_fences,
                                                     zx_duration_t requested_prediction_span);

// Helper for extracting the koid from a kernel object.
zx_koid_t ExtractKoid(const zx::object_base& object);

// Helper for extracting the related koid from a kernel object.
zx_koid_t ExtractRelatedKoid(const zx::object_base& object);

// Helper for extracting the koid from a ViewRef.
zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref);

// Create an unsignalled zx::event.
zx::event CreateEvent();

// Create a std::vector populated with |n| unsignalled zx::event elements.
std::vector<zx::event> CreateEventArray(size_t n);

// Create a std::vector populated with koids of the input vector of zx:event.
std::vector<zx_koid_t> ExtractKoids(const std::vector<zx::event>& events);

// Copy a zx::event.
zx::event CopyEvent(const zx::event& event);

// Copy a zx::eventpair.
zx::eventpair CopyEventpair(const zx::eventpair& eventpair);

// Copy a std::vector of events.
std::vector<zx::event> CopyEventArray(const std::vector<zx::event>& events);

// Synchronously checks whether the event has signalled any of the bits in |signal|.
bool IsEventSignalled(const zx::event& event, zx_signals_t signal);

// Create sysmem allocator.
fuchsia::sysmem::AllocatorSyncPtr CreateSysmemAllocatorSyncPtr(
    const std::string& debug_name_suffix = std::string());

// Create local and dup tokens for sysmem.
SysmemTokens CreateSysmemTokens(fuchsia::sysmem::Allocator_Sync* sysmem_allocator);

// Creates default constraints for |buffer_collection|
fuchsia::sysmem::BufferCollectionConstraints CreateDefaultConstraints(uint32_t buffer_count,
                                                                      uint32_t kWidth,
                                                                      uint32_t kHeight);

// Accounts for floating point rounding errors.
bool RectFContainsPoint(const fuchsia::math::RectF& rect, float x, float y);

// Convenience function
fuchsia::math::RectF ConvertRectToRectF(const fuchsia::math::Rect& rect);

template <std::size_t Dim>
std::string GetArrayString(const std::string& name, const std::array<float, Dim>& array) {
  std::string result = name + ": [";
  for (uint32_t i = 0; i < array.size(); i++) {
    result += std::to_string(array[i]);
    if (i < array.size() - 1) {
      result += ", ";
    }
  }
  result += "]\n";
  return result;
}

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_
