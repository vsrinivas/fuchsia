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

struct magma_buffer* magma_bo_alloc(struct magma_connection* connection, const char* name,
                                    uint32_t size, uint32_t alignment);
struct magma_buffer* magma_bo_alloc_for_render(struct magma_connection* connection,
                                               const char* name, uint32_t size, uint32_t alignment);
// struct magma_buffer *magma_bo_alloc_userptr(struct magma_connection *connection,
//                     const char *name,
//                     void *addr, uint32_t tiling_mode,
//                     uint32_t stride, uint32_t size,
//                     uint32_t flags);
struct magma_buffer* magma_bo_alloc_tiled(struct magma_connection* connection, const char* name,
                                          uint32_t size, uint32_t flags, uint32_t tiling_mode,
                                          uint32_t stride);

void magma_bo_reference(struct magma_buffer* bo);
void magma_bo_unreference(struct magma_buffer* bo);
int32_t magma_bo_map(struct magma_buffer* bo, int32_t write_enable);
int32_t magma_bo_unmap(struct magma_buffer* bo);

int32_t magma_bo_subdata(struct magma_buffer* bo, uint32_t offset, uint32_t size, const void* data);
int32_t magma_bo_get_subdata(struct magma_buffer* bo, uint32_t offset, uint32_t size, void* data);
void magma_bo_wait_rendering(struct magma_buffer* bo);

void magma_bufmgr_set_debug(struct magma_connection* connection, int32_t enable_debug);
void magma_bufmgr_destroy(struct magma_connection* connection);

// int32_t magma_bo_exec(struct magma_buffer *bo, int32_t used,
//               struct drm_clip_rect *cliprects, int32_t num_cliprects, int32_t DR4);
int32_t magma_bo_mrb_exec(struct magma_buffer* bo, int32_t used, void* cliprects,
                          int32_t num_cliprects, int32_t DR4, uint32_t flags);
int32_t magma_bufmgr_check_aperture_space(struct magma_buffer** bo_array, int32_t count);

int32_t magma_bo_emit_reloc(struct magma_buffer* bo, uint32_t offset,
                            struct magma_buffer* target_bo, uint32_t target_offset,
                            uint32_t read_domains, uint32_t write_domain);
// int32_t magma_bo_emit_reloc_fence(struct magma_buffer *bo, uint32_t offset,
//                   struct magma_buffer *target_bo,
//                   uint32_t target_offset,
//                   uint32_t read_domains, uint32_t write_domain);
// int32_t magma_bo_pin(struct magma_buffer *bo, uint32_t alignment);
// int32_t magma_bo_unpin(struct magma_buffer *bo);
int32_t magma_bo_set_tiling(struct magma_buffer* bo, uint32_t* tiling_mode, uint32_t stride);
int32_t magma_bo_get_tiling(struct magma_buffer* bo, uint32_t* tiling_mode, uint32_t* swizzle_mode);
int32_t magma_bo_flink(struct magma_buffer* bo, uint32_t* name);
int32_t magma_bo_busy(struct magma_buffer* bo);
int32_t magma_bo_madvise(struct magma_buffer* bo, int32_t madv);
// int32_t magma_bo_use_48b_address_range(struct magma_buffer *bo, uint32_t enable);
// int32_t magma_bo_set_softpin_offset(struct magma_buffer *bo, uint32_t offset);

// int32_t magma_bo_disable_reuse(struct magma_buffer *bo);
// int32_t magma_bo_is_reusable(struct magma_buffer *bo);
int32_t magma_bo_references(struct magma_buffer* bo, struct magma_buffer* target_bo);

struct magma_connection* magma_bufmgr_gem_init(int32_t fd, int32_t batch_size);
struct magma_buffer* magma_bo_gem_create_from_name(struct magma_connection* connection,
                                                   const char* name, uint32_t handle);
void magma_bufmgr_gem_enable_reuse(struct magma_connection* connection);
void magma_bufmgr_gem_enable_fenced_relocs(struct magma_connection* connection);
// void magma_connection_gem_set_vma_cache_size(struct magma_connection *connection,
//                         int32_t limit);
int32_t magma_gem_bo_map_unsynchronized(struct magma_buffer* bo);
int32_t magma_gem_bo_map_gtt(struct magma_buffer* bo);
// int32_t magma_gem_bo_unmap_gtt(struct magma_buffer *bo);

int32_t magma_gem_bo_get_reloc_count(struct magma_buffer* bo);
void magma_gem_bo_clear_relocs(struct magma_buffer* bo, int32_t start);
// void magma_gem_bo_start_gtt_access(struct magma_buffer *bo, int32_t write_enable);

void magma_connection_gem_set_aub_filename(struct magma_connection* connection,
                                           const char* filename);
void magma_bufmgr_gem_set_aub_dump(struct magma_connection* connection, int32_t enable);
void magma_gem_bo_aub_dump_bmp(struct magma_buffer* bo, int32_t x1, int32_t y1, int32_t width,
                               int32_t height, enum aub_dump_bmp_format format, int32_t pitch,
                               int32_t offset);
void magma_bufmgr_gem_set_aub_annotations(struct magma_buffer* bo,
                                          drm_intel_aub_annotation* annotations, unsigned count);

int32_t magma_get_aperture_sizes(int32_t fd, size_t* mappable, size_t* total);
int32_t magma_bufmgr_gem_get_devid(struct magma_connection* connection);
int32_t magma_gem_bo_wait(struct magma_buffer* bo, int64_t timeout_ns);

struct magma_context* magma_gem_context_create(struct magma_connection* connection);
void magma_gem_context_destroy(struct magma_context* ctx);
int32_t magma_gem_bo_context_exec(struct magma_buffer* bo, struct magma_context* ctx,
                                  int32_t used_batch_len, uint32_t flags);

int32_t magma_bo_gem_export_to_prime(struct magma_buffer* bo, int32_t* prime_fd);
struct magma_buffer* magma_bo_gem_create_from_prime(struct magma_connection* connection,
                                                    int32_t prime_fd, int32_t size);

struct drm_intel_decode* magma_decode_context_alloc(uint32_t devid);
void magma_decode_context_free(struct drm_intel_decode* ctx);
void magma_decode_set_batch_pointer(struct drm_intel_decode* ctx, void* data, uint32_t hw_offset,
                                    int32_t count);
void magma_decode_set_dump_past_end(struct drm_intel_decode* ctx, int32_t dump_past_end);
void magma_decode_set_head_tail(struct drm_intel_decode* ctx, uint32_t head, uint32_t tail);
void magma_decode_set_output_file(struct drm_intel_decode* ctx, FILE* out);
void magma_decode(struct drm_intel_decode* ctx);

int32_t magma_reg_read(struct magma_connection* connection, uint32_t offset, uint64_t* result);

int32_t magma_get_reset_stats(struct magma_context* ctx, uint32_t* reset_count, uint32_t* active,
                              uint32_t* pending);

/** @{ */

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_H_ */
