// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"

#include <lib/fzl/vmar-manager.h>
#include <lib/syslog/cpp/macros.h>

namespace media_audio {

namespace {

fbl::RefPtr<fzl::VmarManager>* CreateVmarManager() {
  // By default, VMOs are mapped into the root VMAR at a random address computed by ASLR. Since the
  // audio mixer expects to map many VMOs, we'll have many mappings spread sparsely across the
  // address space. This makes inefficient use of page tables. By adding a sub-vmar with
  // ZX_VM_COMPACT, we cluster these buffers into a narrow range of the address space, which reduces
  // the number of intermediate page tables required to support the mappings.
  //
  // All MemoryMappedBuffers will need to fit within this VMAR. We want to choose a size here large
  // enough that will accommodate all the mappings required by all clients while also being small
  // enough to avoid unnecessary page table fragmentation.
  //
  // We somewhat-arbitrarily choose 16GB.
  //
  // For historical context, see fxbug.dev/13355 and fxrev.dev/286608.
  constexpr size_t kSize = 16ull * 1024 * 1024 * 1024;
  constexpr zx_vm_option_t kFlags =
      ZX_VM_COMPACT | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_1GB;

  auto ptr = new fbl::RefPtr<fzl::VmarManager>;
  *ptr = fzl::VmarManager::Create(kSize, nullptr, kFlags);
  return ptr;
}

const fbl::RefPtr<fzl::VmarManager>* const vmar_manager = CreateVmarManager();

uint64_t ContentSize(const zx::vmo& vmo) {
  uint64_t content_size;
  if (auto status = vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
      status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "zx::vmo::get_property failed";
  }
  return content_size;
}

}  // namespace

// static
zx::status<std::shared_ptr<MemoryMappedBuffer>> MemoryMappedBuffer::Create(const zx::vmo& vmo,
                                                                           bool writable) {
  zx_vm_option_t flags = ZX_VM_PERM_READ;
  if (writable) {
    flags |= ZX_VM_PERM_WRITE;
  }

  fzl::VmoMapper mapper;
  if (auto status = mapper.Map(vmo, 0, 0, flags, *vmar_manager); status != ZX_OK) {
    return zx::error(status);
  }

  struct WithPublicCtor : public MemoryMappedBuffer {
   public:
    WithPublicCtor(fzl::VmoMapper mapper, size_t content_size)
        : MemoryMappedBuffer(std::move(mapper), content_size) {}
  };
  return zx::ok(std::make_shared<WithPublicCtor>(std::move(mapper), ContentSize(vmo)));
}

// static
std::shared_ptr<MemoryMappedBuffer> MemoryMappedBuffer::CreateOrDie(size_t size, bool writable) {
  zx::vmo vmo;
  if (auto status = zx::vmo::create(size, 0, &vmo); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "zx::vmo::create failed";
  }

  auto buffer_result = Create(vmo, writable);
  FX_CHECK(buffer_result.is_ok()) << "MemoryMappedBuffer::Create failed: "
                                  << buffer_result.status_string();
  return *buffer_result;
}

}  // namespace media_audio
