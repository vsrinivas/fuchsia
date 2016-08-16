// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_H_
#define _MAGMA_H_

#include <stdint.h>
#include <stdio.h>

#include <common/magma_defs.h>

#if defined(__cplusplus)
extern "C" {
#endif

drm_intel_bo* magma_bo_alloc(drm_intel_bufmgr* bufmgr, const char* name, uint32_t size,
                             uint32_t alignment);
drm_intel_bo* magma_bo_alloc_for_render(drm_intel_bufmgr* bufmgr, const char* name,
                                        uint32_t size, uint32_t alignment);
// drm_intel_bo *magma_bo_alloc_userptr(drm_intel_bufmgr *bufmgr,
//                     const char *name,
//                     void *addr, uint32_t tiling_mode,
//                     uint32_t stride, uint32_t size,
//                     uint32_t flags);
drm_intel_bo* magma_bo_alloc_tiled(drm_intel_bufmgr* bufmgr, const char* name, uint32_t size,
                                   uint32_t flags, uint32_t tiling_mode, uint32_t stride);

void magma_bo_reference(drm_intel_bo* bo);
void magma_bo_unreference(drm_intel_bo* bo);
int32_t magma_bo_map(drm_intel_bo* bo, int32_t write_enable);
int32_t magma_bo_unmap(drm_intel_bo* bo);

int32_t magma_bo_subdata(drm_intel_bo* bo, uint32_t offset, uint32_t size, const void* data);
int32_t magma_bo_get_subdata(drm_intel_bo* bo, uint32_t offset, uint32_t size, void* data);
void magma_bo_wait_rendering(drm_intel_bo* bo);

void magma_bufmgr_set_debug(drm_intel_bufmgr* bufmgr, int32_t enable_debug);
void magma_bufmgr_destroy(drm_intel_bufmgr* bufmgr);

// int32_t magma_bo_exec(drm_intel_bo *bo, int32_t used,
//               struct drm_clip_rect *cliprects, int32_t num_cliprects, int32_t DR4);
int32_t magma_bo_mrb_exec(drm_intel_bo* bo, int32_t used, void* cliprects, int32_t num_cliprects, int32_t DR4,
                      uint32_t flags);
int32_t magma_bufmgr_check_aperture_space(drm_intel_bo** bo_array, int32_t count);

int32_t magma_bo_emit_reloc(drm_intel_bo* bo, uint32_t offset, drm_intel_bo* target_bo,
                        uint32_t target_offset, uint32_t read_domains, uint32_t write_domain);
// int32_t magma_bo_emit_reloc_fence(drm_intel_bo *bo, uint32_t offset,
//                   drm_intel_bo *target_bo,
//                   uint32_t target_offset,
//                   uint32_t read_domains, uint32_t write_domain);
// int32_t magma_bo_pin(drm_intel_bo *bo, uint32_t alignment);
// int32_t magma_bo_unpin(drm_intel_bo *bo);
int32_t magma_bo_set_tiling(drm_intel_bo* bo, uint32_t* tiling_mode, uint32_t stride);
int32_t magma_bo_get_tiling(drm_intel_bo* bo, uint32_t* tiling_mode, uint32_t* swizzle_mode);
int32_t magma_bo_flink(drm_intel_bo* bo, uint32_t* name);
int32_t magma_bo_busy(drm_intel_bo* bo);
int32_t magma_bo_madvise(drm_intel_bo* bo, int32_t madv);
// int32_t magma_bo_use_48b_address_range(drm_intel_bo *bo, uint32_t enable);
// int32_t magma_bo_set_softpin_offset(drm_intel_bo *bo, uint32_t offset);

// int32_t magma_bo_disable_reuse(drm_intel_bo *bo);
// int32_t magma_bo_is_reusable(drm_intel_bo *bo);
int32_t magma_bo_references(drm_intel_bo* bo, drm_intel_bo* target_bo);

drm_intel_bufmgr* magma_bufmgr_gem_init(int32_t fd, int32_t batch_size);
drm_intel_bo* magma_bo_gem_create_from_name(drm_intel_bufmgr* bufmgr, const char* name,
                                            uint32_t handle);
void magma_bufmgr_gem_enable_reuse(drm_intel_bufmgr* bufmgr);
void magma_bufmgr_gem_enable_fenced_relocs(drm_intel_bufmgr* bufmgr);
// void magma_bufmgr_gem_set_vma_cache_size(drm_intel_bufmgr *bufmgr,
//                         int32_t limit);
int32_t magma_gem_bo_map_unsynchronized(drm_intel_bo* bo);
int32_t magma_gem_bo_map_gtt(drm_intel_bo* bo);
// int32_t magma_gem_bo_unmap_gtt(drm_intel_bo *bo);

int32_t magma_gem_bo_get_reloc_count(drm_intel_bo* bo);
void magma_gem_bo_clear_relocs(drm_intel_bo* bo, int32_t start);
// void magma_gem_bo_start_gtt_access(drm_intel_bo *bo, int32_t write_enable);

void magma_bufmgr_gem_set_aub_filename(drm_intel_bufmgr* bufmgr, const char* filename);
void magma_bufmgr_gem_set_aub_dump(drm_intel_bufmgr* bufmgr, int32_t enable);
void magma_gem_bo_aub_dump_bmp(drm_intel_bo* bo, int32_t x1, int32_t y1, int32_t width, int32_t height,
                               enum aub_dump_bmp_format format, int32_t pitch, int32_t offset);
void magma_bufmgr_gem_set_aub_annotations(drm_intel_bo* bo, drm_intel_aub_annotation* annotations,
                                          unsigned count);

int32_t magma_get_aperture_sizes(int32_t fd, size_t* mappable, size_t* total);
int32_t magma_bufmgr_gem_get_devid(drm_intel_bufmgr* bufmgr);
int32_t magma_gem_bo_wait(drm_intel_bo* bo, int64_t timeout_ns);

drm_intel_context* magma_gem_context_create(drm_intel_bufmgr* bufmgr);
void magma_gem_context_destroy(drm_intel_context* ctx);
int32_t magma_gem_bo_context_exec(drm_intel_bo* bo, drm_intel_context* ctx, int32_t used,
                              uint32_t flags);

int32_t magma_bo_gem_export_to_prime(drm_intel_bo* bo, int32_t* prime_fd);
drm_intel_bo* magma_bo_gem_create_from_prime(drm_intel_bufmgr* bufmgr, int32_t prime_fd, int32_t size);

struct drm_intel_decode* magma_decode_context_alloc(uint32_t devid);
void magma_decode_context_free(struct drm_intel_decode* ctx);
void magma_decode_set_batch_pointer(struct drm_intel_decode* ctx, void* data, uint32_t hw_offset,
                                    int32_t count);
void magma_decode_set_dump_past_end(struct drm_intel_decode* ctx, int32_t dump_past_end);
void magma_decode_set_head_tail(struct drm_intel_decode* ctx, uint32_t head, uint32_t tail);
void magma_decode_set_output_file(struct drm_intel_decode* ctx, FILE* out);
void magma_decode(struct drm_intel_decode* ctx);

int32_t magma_reg_read(drm_intel_bufmgr* bufmgr, uint32_t offset, uint64_t* result);

int32_t magma_get_reset_stats(drm_intel_context* ctx, uint32_t* reset_count, uint32_t* active,
                          uint32_t* pending);

/** @{ */

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_H_ */
