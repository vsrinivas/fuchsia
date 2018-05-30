// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/host_memory.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo HostMemory::kTypeInfo = {
    ResourceType::kMemory | ResourceType::kHostMemory, "HostMemory"};

HostMemory::HostMemory(Session* session, scenic::ResourceId id, zx::vmo vmo,
                       uint64_t vmo_size)
    : Memory(session, id, HostMemory::kTypeInfo),
      shared_vmo_(fxl::MakeRefCounted<fsl::SharedVmo>(std::move(vmo),
                                                      ZX_VM_FLAG_PERM_READ)),
      size_(vmo_size) {}

HostMemoryPtr HostMemory::New(Session* session, scenic::ResourceId id,
                              vk::Device device,
                              ::fuchsia::ui::gfx::MemoryArgs args,
                              ErrorReporter* error_reporter) {
  if (args.memory_type != fuchsia::images::MemoryType::HOST_MEMORY) {
    error_reporter->ERROR() << "scenic::gfx::HostMemory::New(): "
                               "Memory must be of type HOST_MEMORY.";
    return nullptr;
  }

  return New(session, id, device, std::move(args.vmo), error_reporter);
}

HostMemoryPtr HostMemory::New(Session* session, scenic::ResourceId id,
                              vk::Device device, zx::vmo vmo,
                              ErrorReporter* error_reporter) {
  uint64_t vmo_size;
  vmo.get_size(&vmo_size);
  return fxl::MakeRefCounted<HostMemory>(session, id, std::move(vmo), vmo_size);
}

}  // namespace gfx
}  // namespace scenic
