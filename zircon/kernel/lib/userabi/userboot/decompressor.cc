// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "decompressor.h"

#include <assert.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stddef.h>

#include <zstd/zstd.h>

#include "bump_allocator.h"
#include "mapper.h"
#include "util.h"

namespace {

constexpr size_t kHeapSize = 5u << 20;

void* zstd_alloc(void* opaque, size_t size) {
  BumpAllocator* allocator = static_cast<BumpAllocator*>(opaque);
  return allocator->malloc(size);
}

void zstd_free(void* opaque, void* address) {
  BumpAllocator* allocator = static_cast<BumpAllocator*>(opaque);
  allocator->free(address);
}

}  // namespace

static zx_status_t decompress_with_zstd(const zx::vmar& vmar, std::byte* input_data,
                                        size_t input_size, std::byte* output_data,
                                        size_t output_size) {
  BumpAllocator allocator(&vmar);
  zx_status_t status = allocator.Init(kHeapSize);
  if (status != ZX_OK) {
    return status;
  }

  ZSTD_DCtx* dctx = ZSTD_createDCtx_advanced({&zstd_alloc, &zstd_free, &allocator});
  auto rc = ZSTD_decompressDCtx(dctx, output_data, output_size, input_data, input_size);
  ZSTD_freeDCtx(dctx);

  if (ZSTD_isError(rc) || rc != output_size) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return ZX_OK;
}

zx_status_t zbi_decompress(zx_handle_t log, const zx::vmar& vmar, const zx::vmo& input_vmo,
                           uint64_t input_offset, size_t input_size, const zx::vmo& output_vmo,
                           uint64_t output_offset, size_t output_size) {
  // Magic number at the start of a compressed image.
  // Reading this much is enough to identify the format.
  using Magic = uint32_t;

  static constexpr Magic kZstdMagic = 0xFD2FB528;

  Magic magic;
  zx_status_t status = input_vmo.read(&magic, input_offset, sizeof(magic));
  if (status != ZX_OK) {
    check(log, status, "failed to read magic from ZBI");
    return status;
  }

  Mapper input(&vmar);
  status = input.Map(ZX_VM_PERM_READ | ZX_VM_MAP_RANGE, input_vmo, input_offset, input_size);
  if (status != ZX_OK) {
    check(log, status, "failed to map ZBI for decompresion");
    return status;
  }

  Mapper output(&vmar);
  status = output.Map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE, output_vmo,
                      output_offset, output_size);
  if (status != ZX_OK) {
    check(log, status, "failed to map output VMO for ZBI decompresion");
    return status;
  }

  if (magic == kZstdMagic) {
    return decompress_with_zstd(vmar, input.data(), input_size, output.data(), output_size);
  } else {
    return ZX_ERR_NOT_FOUND;
  }
}
