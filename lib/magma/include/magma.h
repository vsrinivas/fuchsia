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

drm_intel_bo* magma_bo_alloc(drm_intel_bufmgr* bufmgr, const char* name, unsigned long size,
                             unsigned int alignment);
drm_intel_bo* magma_bo_alloc_for_render(drm_intel_bufmgr* bufmgr, const char* name,
                                        unsigned long size, unsigned int alignment);
// drm_intel_bo *magma_bo_alloc_userptr(drm_intel_bufmgr *bufmgr,
//                     const char *name,
//                     void *addr, uint32_t tiling_mode,
//                     uint32_t stride, unsigned long size,
//                     unsigned long flags);
drm_intel_bo* magma_bo_alloc_tiled(drm_intel_bufmgr* bufmgr, const char* name, unsigned long size,
                                   unsigned long flags, uint32_t tiling_mode, unsigned long stride);

void magma_bo_reference(drm_intel_bo* bo);
void magma_bo_unreference(drm_intel_bo* bo);
int magma_bo_map(drm_intel_bo* bo, int write_enable);
int magma_bo_unmap(drm_intel_bo* bo);

int magma_bo_subdata(drm_intel_bo* bo, unsigned long offset, unsigned long size, const void* data);
int magma_bo_get_subdata(drm_intel_bo* bo, unsigned long offset, unsigned long size, void* data);
void magma_bo_wait_rendering(drm_intel_bo* bo);

void magma_bufmgr_set_debug(drm_intel_bufmgr* bufmgr, int enable_debug);
void magma_bufmgr_destroy(drm_intel_bufmgr* bufmgr);

// int magma_bo_exec(drm_intel_bo *bo, int used,
//               struct drm_clip_rect *cliprects, int num_cliprects, int DR4);
int magma_bo_mrb_exec(drm_intel_bo* bo, int used, void* cliprects, int num_cliprects, int DR4,
                      unsigned int flags);
int magma_bufmgr_check_aperture_space(drm_intel_bo** bo_array, int count);

int magma_bo_emit_reloc(drm_intel_bo* bo, uint32_t offset, drm_intel_bo* target_bo,
                        uint32_t target_offset, uint32_t read_domains, uint32_t write_domain);
// int magma_bo_emit_reloc_fence(drm_intel_bo *bo, uint32_t offset,
//                   drm_intel_bo *target_bo,
//                   uint32_t target_offset,
//                   uint32_t read_domains, uint32_t write_domain);
// int magma_bo_pin(drm_intel_bo *bo, uint32_t alignment);
// int magma_bo_unpin(drm_intel_bo *bo);
int magma_bo_set_tiling(drm_intel_bo* bo, uint32_t* tiling_mode, uint32_t stride);
int magma_bo_get_tiling(drm_intel_bo* bo, uint32_t* tiling_mode, uint32_t* swizzle_mode);
int magma_bo_flink(drm_intel_bo* bo, uint32_t* name);
int magma_bo_busy(drm_intel_bo* bo);
int magma_bo_madvise(drm_intel_bo* bo, int madv);
// int magma_bo_use_48b_address_range(drm_intel_bo *bo, uint32_t enable);
// int magma_bo_set_softpin_offset(drm_intel_bo *bo, uint64_t offset);

// int magma_bo_disable_reuse(drm_intel_bo *bo);
// int magma_bo_is_reusable(drm_intel_bo *bo);
int magma_bo_references(drm_intel_bo* bo, drm_intel_bo* target_bo);

drm_intel_bufmgr* magma_bufmgr_gem_init(int fd, int batch_size);
drm_intel_bo* magma_bo_gem_create_from_name(drm_intel_bufmgr* bufmgr, const char* name,
                                            unsigned int handle);
void magma_bufmgr_gem_enable_reuse(drm_intel_bufmgr* bufmgr);
void magma_bufmgr_gem_enable_fenced_relocs(drm_intel_bufmgr* bufmgr);
// void magma_bufmgr_gem_set_vma_cache_size(drm_intel_bufmgr *bufmgr,
//                         int limit);
int magma_gem_bo_map_unsynchronized(drm_intel_bo* bo);
int magma_gem_bo_map_gtt(drm_intel_bo* bo);
// int magma_gem_bo_unmap_gtt(drm_intel_bo *bo);

int magma_gem_bo_get_reloc_count(drm_intel_bo* bo);
void magma_gem_bo_clear_relocs(drm_intel_bo* bo, int start);
// void magma_gem_bo_start_gtt_access(drm_intel_bo *bo, int write_enable);

void magma_bufmgr_gem_set_aub_filename(drm_intel_bufmgr* bufmgr, const char* filename);
void magma_bufmgr_gem_set_aub_dump(drm_intel_bufmgr* bufmgr, int enable);
void magma_gem_bo_aub_dump_bmp(drm_intel_bo* bo, int x1, int y1, int width, int height,
                               enum aub_dump_bmp_format format, int pitch, int offset);
void magma_bufmgr_gem_set_aub_annotations(drm_intel_bo* bo, drm_intel_aub_annotation* annotations,
                                          unsigned count);

int magma_get_aperture_sizes(int fd, size_t* mappable, size_t* total);
int magma_bufmgr_gem_get_devid(drm_intel_bufmgr* bufmgr);
int magma_gem_bo_wait(drm_intel_bo* bo, int64_t timeout_ns);

drm_intel_context* magma_gem_context_create(drm_intel_bufmgr* bufmgr);
void magma_gem_context_destroy(drm_intel_context* ctx);
int magma_gem_bo_context_exec(drm_intel_bo* bo, drm_intel_context* ctx, int used,
                              unsigned int flags);

int magma_bo_gem_export_to_prime(drm_intel_bo* bo, int* prime_fd);
drm_intel_bo* magma_bo_gem_create_from_prime(drm_intel_bufmgr* bufmgr, int prime_fd, int size);

struct drm_intel_decode* magma_decode_context_alloc(uint32_t devid);
void magma_decode_context_free(struct drm_intel_decode* ctx);
void magma_decode_set_batch_pointer(struct drm_intel_decode* ctx, void* data, uint32_t hw_offset,
                                    int count);
void magma_decode_set_dump_past_end(struct drm_intel_decode* ctx, int dump_past_end);
void magma_decode_set_head_tail(struct drm_intel_decode* ctx, uint32_t head, uint32_t tail);
void magma_decode_set_output_file(struct drm_intel_decode* ctx, FILE* out);
void magma_decode(struct drm_intel_decode* ctx);

int magma_reg_read(drm_intel_bufmgr* bufmgr, uint32_t offset, uint64_t* result);

int magma_get_reset_stats(drm_intel_context* ctx, uint32_t* reset_count, uint32_t* active,
                          uint32_t* pending);

/** @{ */

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_H_ */
