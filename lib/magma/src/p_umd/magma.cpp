// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "magma.h"
#include "magma_umd.h"
#include <assert.h>
#include <stdio.h>

#include "util/dlog.h"

#define UNIMPLEMENTED(x)                                                                           \
    DLOG("magma unimplemented api: %s\n", x);                                                      \
    assert(0)

drm_intel_bo* magma_bo_alloc(drm_intel_bufmgr* bufmgr, const char* name, unsigned long size,
                             unsigned int alignment)
{
    UNIMPLEMENTED("magma_bo_alloc");
    return 0;
}

drm_intel_bo* magma_bo_alloc_for_render(drm_intel_bufmgr* bufmgr, const char* name,
                                        unsigned long size, unsigned int alignment)
{
    UNIMPLEMENTED("magma_bo_alloc_for_render");
    return 0;
}

drm_intel_bo* magma_bo_alloc_tiled(drm_intel_bufmgr* bufmgr, const char* name, int x, int y,
                                   int bytes_per_pixel, uint32_t* tiling_mode, unsigned long* pitch,
                                   unsigned long flags)
{
    DLOG("magma_bo_alloc_tiled %s\n", name);
    // stride=pitch
    // TODO(MA-2)
    uint32_t stride = *pitch;
    auto bo = bufmgr->AllocTiledBufferObject(name, static_cast<uint32_t>(x), y, bytes_per_pixel,
                                             *tiling_mode, &stride);

    *pitch = stride;
    return bo;
}

int magma_bo_busy(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_bo_busy");
    return 0;
}

int magma_bo_emit_reloc(drm_intel_bo* bo, uint32_t offset, drm_intel_bo* target_bo,
                        uint32_t target_offset, uint32_t read_domains, uint32_t write_domain)
{
    UNIMPLEMENTED("magma_bo_emit_reloc");
    return 0;
}

int magma_bo_flink(drm_intel_bo* bo, uint32_t* name)
{
    UNIMPLEMENTED("mamga_bo_flink");
    return 0;
}

drm_intel_bo* magma_bo_gem_create_from_name(drm_intel_bufmgr* bufmgr, const char* name,
                                            unsigned int handle)
{
    UNIMPLEMENTED("magma_bo_gem_create_from_name");
    return 0;
}

drm_intel_bo* magma_bo_gem_create_from_prime(drm_intel_bufmgr* bufmgr, int prime_fd, int size)
{
    UNIMPLEMENTED("magma_bo_gem_create_from_prime");
    return 0;
}

int magma_bo_gem_export_to_prime(drm_intel_bo* bo, int* prime_fd)
{
    UNIMPLEMENTED("magma_bo_gem_export_to_prime");
    return 0;
}

int magma_bo_get_subdata(drm_intel_bo* bo, unsigned long offset, unsigned long size, void* data)
{
    UNIMPLEMENTED("magma_bo_get_subdata");
    return 0;
}

int magma_bo_get_tiling(drm_intel_bo* bo, uint32_t* tiling_mode, uint32_t* swizzle_mode)
{
    UNIMPLEMENTED("magma_bo_get_tiling");
    return 0;
}

int magma_bo_madvise(drm_intel_bo* bo, int madv)
{
    UNIMPLEMENTED("magma_bo_madvise");
    return 0;
}

int magma_bo_map(drm_intel_bo* bo, int write_enable)
{
    UNIMPLEMENTED("magma_bo_map");
    return 0;
}

int magma_bo_mrb_exec(drm_intel_bo* bo, int used, struct drm_clip_rect* cliprects,
                      int num_cliprects, int DR4, unsigned int flags)
{
    UNIMPLEMENTED("magma_bo_mrb_exec");
    return 0;
}

void magma_bo_reference(drm_intel_bo* bo) { UNIMPLEMENTED("magma_bo_reference"); }

int magma_bo_references(drm_intel_bo* bo, drm_intel_bo* target_bo)
{
    UNIMPLEMENTED("magma_bo_references");
    return 0;
}

int magma_bo_subdata(drm_intel_bo* bo, unsigned long offset, unsigned long size, const void* data)
{
    UNIMPLEMENTED("magma_bo_subdata");
    return 0;
}

int magma_bo_unmap(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_bo_unmap");
    return 0;
}

void magma_bo_unreference(drm_intel_bo* bo) { UNIMPLEMENTED("magma_bo_unreference"); }

void magma_bo_wait_rendering(drm_intel_bo* bo) { UNIMPLEMENTED("magma_bo_wait_rendering"); }

int magma_bufmgr_check_aperture_space(drm_intel_bo** bo_array, int count)
{
    UNIMPLEMENTED("magma_bufmgr_check_aperture_space");
    return 0;
}

void magma_bufmgr_destroy(drm_intel_bufmgr* bufmgr) { UNIMPLEMENTED("magma_bufmgr_destroy"); }

void magma_bufmgr_gem_enable_fenced_relocs(drm_intel_bufmgr* bufmgr)
{
    DLOG("magma_bufmgr_gem_enable_fenced_relocs - STUB\n");
}

void magma_bufmgr_gem_enable_reuse(drm_intel_bufmgr* bufmgr)
{
    UNIMPLEMENTED("magma_bufmgr_gem_enable_reuse");
}

int magma_bufmgr_gem_get_devid(drm_intel_bufmgr* umd)
{
    DLOG("magma_bufmgr_gem_get_devid\n");
    int id = umd->GetDeviceId();
    DLOG("returning id 0x%x\n", id);
    return id;
}

// TODO: remove fd param (drm-specific)
drm_intel_bufmgr* magma_bufmgr_gem_init(int fd, int batch_size)
{
    DLOG("magma_bufmgr_gem_init - IGNORING fd param %d\n", fd);
    return MagmaUmd::New(0);
}

void magma_bufmgr_gem_set_aub_annotations(drm_intel_bo* bo, drm_intel_aub_annotation* annotations,
                                          unsigned count)
{
    UNIMPLEMENTED("magma_bufmgr_gem_set_aub_annotations");
}

void magma_bufmgr_gem_set_aub_dump(drm_intel_bufmgr* bufmgr, int enable)
{
    UNIMPLEMENTED("magma_bufmgr_gem_set_aub_dump");
}

void magma_bufmgr_set_debug(drm_intel_bufmgr* bufmgr, int enable_debug)
{
    UNIMPLEMENTED("magma_bufmgr_set_debug");
}

void magma_decode(struct drm_intel_decode* ctx) { UNIMPLEMENTED("magma_decode"); }

struct drm_intel_decode* magma_decode_context_alloc(uint32_t devid)
{
    UNIMPLEMENTED("magma_decode_context_alloc");
    return 0;
}

void magma_decode_context_free(struct drm_intel_decode* ctx)
{
    UNIMPLEMENTED("magma_decode_context_free");
}

void magma_decode_set_batch_pointer(struct drm_intel_decode* ctx, void* data, uint32_t hw_offset,
                                    int count)
{
    UNIMPLEMENTED("magma_decode_set_batch_pointer");
}

void magma_decode_set_output_file(struct drm_intel_decode* ctx, FILE* out)
{
    UNIMPLEMENTED("magma_decode_set_output_file");
}

void magma_gem_bo_aub_dump_bmp(drm_intel_bo* bo, int x1, int y1, int width, int height,
                               enum aub_dump_bmp_format format, int pitch, int offset)
{
    UNIMPLEMENTED("magma_gem_bo_aub_dump_bmp");
}

void magma_gem_bo_clear_relocs(drm_intel_bo* bo, int start)
{
    UNIMPLEMENTED("magma_gem_bo_clear_relocs");
}

int magma_gem_bo_get_reloc_count(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_gem_bo_get_reloc_count");
    return 0;
}

int magma_gem_bo_map_gtt(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_gem_bo_map_gtt");
    return 0;
}

int magma_gem_bo_map_unsynchronized(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_gem_bo_map_unsynchronized");
    return 0;
}

int magma_gem_bo_wait(drm_intel_bo* bo, int64_t timeout_ns)
{
    UNIMPLEMENTED("magma_gem_bo_wait");
    return 0;
}

drm_intel_context* magma_gem_context_create(drm_intel_bufmgr* bufmgr)
{
    UNIMPLEMENTED("magma_gem_context_create");
    return 0;
}

void magma_gem_context_destroy(drm_intel_context* ctx)
{
    UNIMPLEMENTED("magma_gem_context_destroy");
}

int magma_gem_bo_context_exec(drm_intel_bo* bo, drm_intel_context* ctx, int used,
                              unsigned int flags)
{
    UNIMPLEMENTED("magma_gem_bo_context_exec");
    return 0;
}

int magma_get_aperture_sizes(int fd, size_t* mappable, size_t* total)
{
    UNIMPLEMENTED("magma_get_aperture_sizes");
    return 0;
}

int magma_get_reset_stats(drm_intel_context* ctx, uint32_t* reset_count, uint32_t* active,
                          uint32_t* pending)
{
    UNIMPLEMENTED("magma_get_reset_stats");
    return 0;
}

int magma_reg_read(drm_intel_bufmgr* bufmgr, uint32_t offset, uint64_t* result)
{
    UNIMPLEMENTED("magma_reg_read");
    return 0;
}

