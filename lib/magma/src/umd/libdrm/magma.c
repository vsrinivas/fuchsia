// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "intel_bufmgr.h"
#include <assert.h>
#include <stdio.h>

#define UNIMPLEMENTED(x)                                                                           \
    fprintf(stderr, "magma unimplemented api: %s\n", x);                                           \
    assert(0)

static const int trace = 1;

drm_intel_bo* magma_bo_alloc(drm_intel_bufmgr* bufmgr, const char* name, unsigned long size,
                             unsigned int alignment)
{
    if (trace)
        fprintf(stderr, "magma_bo_alloc %p %s\n", bufmgr, name);
    return drm_intel_bo_alloc(bufmgr, name, size, alignment);
}

drm_intel_bo* magma_bo_alloc_for_render(drm_intel_bufmgr* bufmgr, const char* name,
                                        unsigned long size, unsigned int alignment)
{
    UNIMPLEMENTED("magma_bo_alloc_for_render");
    return 0;
}

drm_intel_bo* magma_bo_alloc_tiled(drm_intel_bufmgr* bufmgr, const char* name, int x, int y,
                                   int cpp, uint32_t* tiling_mode, unsigned long* pitch,
                                   unsigned long flags)
{
    if (trace)
        fprintf(stderr, "magma_bo_alloc_tiled %p %s\n", bufmgr, name);
    return drm_intel_bo_alloc_tiled(bufmgr, name, x, y, cpp, tiling_mode, pitch, flags);
}

int magma_bo_busy(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_bo_busy");
    return 0;
}

int magma_bo_emit_reloc(drm_intel_bo* bo, uint32_t offset, drm_intel_bo* target_bo,
                        uint32_t target_offset, uint32_t read_domains, uint32_t write_domain)
{
    if (trace)
        fprintf(stderr, "magma_bo_emit_reloc %p\n", bo);
    return drm_intel_bo_emit_reloc(bo, offset, target_bo, target_offset, read_domains,
                                   write_domain);
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
    if (trace)
        fprintf(stderr, "magma_bo_gem_export_to_prime %p\n", bo);
    return drm_intel_bo_gem_export_to_prime(bo, prime_fd);
}

int magma_bo_get_subdata(drm_intel_bo* bo, unsigned long offset, unsigned long size, void* data)
{
    UNIMPLEMENTED("magma_bo_get_subdata");
    return 0;
}

int magma_bo_get_tiling(drm_intel_bo* bo, uint32_t* tiling_mode, uint32_t* swizzle_mode)
{
    if (trace)
        fprintf(stderr, "magma_bo_get_tiling %p\n", bo);
    return drm_intel_bo_get_tiling(bo, tiling_mode, swizzle_mode);
}

int magma_bo_madvise(drm_intel_bo* bo, int madv)
{
    UNIMPLEMENTED("magma_bo_madvise");
    return 0;
}

int magma_bo_map(drm_intel_bo* bo, int write_enable)
{
    if (trace)
        fprintf(stderr, "magma_bo_map %p %d\n", bo, write_enable);
    return drm_intel_bo_map(bo, write_enable);
}

int magma_bo_mrb_exec(drm_intel_bo* bo, int used, struct drm_clip_rect* cliprects,
                      int num_cliprects, int DR4, unsigned int flags)
{
    UNIMPLEMENTED("magma_bo_mrb_exec");
    return 0;
}

void magma_bo_reference(drm_intel_bo* bo)
{
    if (trace)
        fprintf(stderr, "magma_bo_reference %p\n", bo);
    drm_intel_bo_reference(bo);
}

int magma_bo_references(drm_intel_bo* bo, drm_intel_bo* target_bo)
{
    UNIMPLEMENTED("magma_bo_references");
    return 0;
}

int magma_bo_subdata(drm_intel_bo* bo, unsigned long offset, unsigned long size, const void* data)
{
    if (trace)
        fprintf(stderr, "magma_bo_subdata %p 0x%lx 0x%lx\n", bo, offset, size);
    return drm_intel_bo_subdata(bo, offset, size, data);
}

int magma_bo_unmap(drm_intel_bo* bo)
{
    if (trace)
        fprintf(stderr, "magma_bo_unmap %p\n", bo);
    return drm_intel_bo_unmap(bo);
}

void magma_bo_unreference(drm_intel_bo* bo)
{
    if (trace)
        fprintf(stderr, "magma_bo_unreference %p\n", bo);
    drm_intel_bo_unreference(bo);
}

void magma_bo_wait_rendering(drm_intel_bo* bo)
{
    if (trace)
        fprintf(stderr, "magma_bo_wait_rendering %p\n", bo);
    drm_intel_bo_wait_rendering(bo);
}

int magma_bufmgr_check_aperture_space(drm_intel_bo** bo_array, int count)
{
    if (trace)
        fprintf(stderr, "magma_bufmgr_check_aperture_space %p %d\n", bo_array, count);
    return drm_intel_bufmgr_check_aperture_space(bo_array, count);
}

void magma_bufmgr_destroy(drm_intel_bufmgr* bufmgr) { UNIMPLEMENTED("magma_bufmgr_destroy"); }

void magma_bufmgr_gem_enable_fenced_relocs(drm_intel_bufmgr* bufmgr)
{
    if (trace)
        fprintf(stderr, "magma_bufmgr_gem_enable_fenced_relocs %p\n", bufmgr);
    return drm_intel_bufmgr_gem_enable_fenced_relocs(bufmgr);
}

void magma_bufmgr_gem_enable_reuse(drm_intel_bufmgr* bufmgr)
{
    if (trace)
        fprintf(stderr, "magma_bufmgr_gem_enable_reuse %p\n", bufmgr);
    return drm_intel_bufmgr_gem_enable_reuse(bufmgr);
}

int magma_bufmgr_gem_get_devid(drm_intel_bufmgr* bufmgr)
{
    if (trace)
        fprintf(stderr, "magma_bufmgr_gem_get_devid %p\n", bufmgr);
    return drm_intel_bufmgr_gem_get_devid(bufmgr);
}

drm_intel_bufmgr* magma_bufmgr_gem_init(int fd, int batch_size)
{
    if (trace)
        fprintf(stderr, "magma_bufmgr_gem_init %d, %d\n", fd, batch_size);
    return drm_intel_bufmgr_gem_init(fd, batch_size);
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
    if (trace)
        fprintf(stderr, "magma_gem_bo_clear_relocs %p %d\n", bo, start);
    drm_intel_gem_bo_clear_relocs(bo, start);
}

int magma_gem_bo_get_reloc_count(drm_intel_bo* bo)
{
    if (trace)
        fprintf(stderr, "magma_gem_bo_get_reloc_count %p\n", bo);
    return drm_intel_gem_bo_get_reloc_count(bo);
}

int magma_gem_bo_map_gtt(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_gem_bo_map_gtt");
    return 0;
}

int magma_gem_bo_map_unsynchronized(drm_intel_bo* bo)
{
    if (trace)
        fprintf(stderr, "magma_gem_bo_map_unsynchronized %p\n", bo);
    return drm_intel_gem_bo_map_unsynchronized(bo);
}

int magma_gem_bo_wait(drm_intel_bo* bo, int64_t timeout_ns)
{
    UNIMPLEMENTED("magma_gem_bo_wait");
    return 0;
}

drm_intel_context* magma_gem_context_create(drm_intel_bufmgr* bufmgr)
{
    if (trace)
        fprintf(stderr, "magma_gem_context_create %p\n", bufmgr);
    return drm_intel_gem_context_create(bufmgr);
}

void magma_gem_context_destroy(drm_intel_context* ctx)
{
    UNIMPLEMENTED("magma_gem_context_destroy");
}

int magma_gem_bo_context_exec(drm_intel_bo* bo, drm_intel_context* ctx, int used,
                              unsigned int flags)
{
    if (trace)
        fprintf(stderr, "magma_gem_bo_context_exec %p %p %d 0x%x\n", bo, ctx, used, flags);
    return drm_intel_gem_bo_context_exec(bo, ctx, used, flags);
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
    if (trace)
        fprintf(stderr, "magma_reg_read %p 0x%x\n", bufmgr, offset);
    return drm_intel_reg_read(bufmgr, offset, result);
}
