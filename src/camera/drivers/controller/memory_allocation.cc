// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/memory_allocation.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>

#include <sstream>

#include "src/lib/fsl/handles/object_info.h"

namespace camera {

constexpr auto kTag = "camera_controller";

ControllerMemoryAllocator::ControllerMemoryAllocator(const ddk::SysmemProtocolClient& sysmem)
    : sysmem_(sysmem) {
  sysmem_.Connect(sysmem_allocator_.NewRequest().TakeChannel());
}

template <typename T>
static std::string FormatMinMax(const T& v_min, const T& v_max) {
  if (v_min == v_max) {
    return std::to_string(v_min);
  }
  bool lowest_min = v_min == std::numeric_limits<T>::min();
  bool highest_max = v_max == std::numeric_limits<T>::max();
  if (lowest_min && highest_max) {
    return "any";
  }
  if (lowest_min) {
    return "≤ " + std::to_string(v_max);
  }
  if (highest_max) {
    return "≥ " + std::to_string(v_min);
  }
  return std::to_string(v_min) + " - " + std::to_string(v_max);
}

static std::string FormatConstraints(
    const std::vector<fuchsia::sysmem::BufferCollectionConstraints>& constraints) {
  std::stringstream ss;
  for (uint32_t i = 0; i < constraints.size(); ++i) {
    const auto& element = constraints[i];
    ss << "  [" << i << "] " << element.min_buffer_count_for_camping << " camping, "
       << element.min_buffer_count << " total\n";
    for (uint32_t j = 0; j < element.image_format_constraints_count; ++j) {
      const auto& format = element.image_format_constraints[j];
      ss << "    [" << j << "] ("
         << FormatMinMax(format.required_min_coded_width, format.required_max_coded_width)
         << ") x ("
         << FormatMinMax(format.required_min_coded_height, format.required_max_coded_height)
         << ")\n";
    }
  }
  return ss.str();
}

zx_status_t ControllerMemoryAllocator::AllocateSharedMemory(
    const std::vector<fuchsia::sysmem::BufferCollectionConstraints>& constraints,
    BufferCollection& out_buffer_collection, const std::string& name) const {
  TRACE_DURATION("camera", "ControllerMemoryAllocator::AllocateSharedMemory");

  FX_LOGST(INFO, kTag) << "AllocateSharedMemory (name = " << name
                       << ") with the following constraints:\n"
                       << FormatConstraints(constraints);

  auto num_constraints = constraints.size();

  if (!num_constraints) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Create tokens which we'll hold on to to get our buffer_collection.
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> tokens(num_constraints);

  // Start the allocation process.
  auto status = sysmem_allocator_->AllocateSharedCollection(tokens[0].NewRequest());
  if (status != ZX_OK) {
    FX_LOGST(ERROR, kTag) << "Failed to create token";
    return status;
  }

  // Duplicate the tokens.
  for (uint32_t i = 1; i < num_constraints; i++) {
    status = tokens[0]->Duplicate(std::numeric_limits<uint32_t>::max(), tokens[i].NewRequest());
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Failed to duplicate token";
      return status;
    }
  }

  // Now convert into a Logical BufferCollection:
  std::vector<fuchsia::sysmem::BufferCollectionSyncPtr> buffer_collections(num_constraints);

  status = sysmem_allocator_->BindSharedCollection(std::move(tokens[0]),
                                                   buffer_collections[0].NewRequest());
  if (status != ZX_OK) {
    FX_LOGST(ERROR, kTag) << "Failed to create logical buffer collection";
    return status;
  }

  status = buffer_collections[0]->Sync();
  if (status != ZX_OK) {
    FX_LOGST(ERROR, kTag) << "Failed to sync";
    return status;
  }

  constexpr uint32_t kNamePriority = 10u;
  buffer_collections[0]->SetName(kNamePriority, name);

  // Create rest of the logical buffer collections
  for (uint32_t i = 1; i < num_constraints; i++) {
    status = sysmem_allocator_->BindSharedCollection(std::move(tokens[i]),
                                                     buffer_collections[i].NewRequest());
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Failed to create logical buffer collection";
      return status;
    }
  }

  // Set constraints
  for (uint32_t i = 0; i < num_constraints; i++) {
    status = buffer_collections[i]->SetConstraints(true, constraints[i]);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Failed to set buffer collection constraints";
      return status;
    }
  }

  zx_status_t allocation_status;
  status = buffer_collections[0]->WaitForBuffersAllocated(&allocation_status,
                                                          &out_buffer_collection.buffers);
  if (status != ZX_OK) {
    FX_LOGST(ERROR, kTag) << "Failed to wait for buffer collection info.";
    return status;
  }
  if (allocation_status != ZX_OK) {
    FX_LOGST(ERROR, kTag) << "Failed to allocate buffer collection.";
    return allocation_status;
  }

  // Leave first collection handle open to return
  out_buffer_collection.ptr = buffer_collections[0].Unbind().Bind();

  for (uint32_t i = 1; i < num_constraints; i++) {
    status = buffer_collections[i]->Close();
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Failed to close producer buffer collection";
      return status;
    }
  }

  return ZX_OK;
}

fuchsia::sysmem::BufferCollectionHandle ControllerMemoryAllocator::AttachObserverCollection(
    fuchsia::sysmem::BufferCollectionTokenHandle& token) {
  // Temporarily bind the provided token so it can be duplicated.
  auto ptr = token.BindSync();
  fuchsia::sysmem::BufferCollectionTokenHandle observer;
  ZX_ASSERT(ptr->Duplicate(ZX_RIGHT_SAME_RIGHTS, observer.NewRequest()) == ZX_OK);
  ZX_ASSERT(ptr->Sync() == ZX_OK);
  // Return the channel to the provided token.
  token = ptr.Unbind();

  // Bind the new token to a collection, set constraints, and return the client end to the caller.
  fuchsia::sysmem::BufferCollectionSyncPtr collection;
  ZX_ASSERT(sysmem_allocator_->BindSharedCollection(std::move(observer), collection.NewRequest()) ==
            ZX_OK);
  ZX_ASSERT(collection->SetConstraints(true, {.usage{.none = fuchsia::sysmem::noneUsage}}) ==
            ZX_OK);
  ZX_ASSERT(collection->Sync() == ZX_OK);
  return collection.Unbind();
}

}  // namespace camera
