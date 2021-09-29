// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_
#define SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/zx/event.h>

namespace utils {

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

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_
