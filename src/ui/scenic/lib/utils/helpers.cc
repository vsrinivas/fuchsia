// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/helpers.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/handles/object_info.h"

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
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  FX_DCHECK(status == ZX_OK);
  sysmem_allocator->SetDebugClientInfo(fsl::GetCurrentProcessName() + debug_name_suffix,
                                       fsl::GetCurrentProcessKoid());
  return sysmem_allocator;
}

}  // namespace utils
