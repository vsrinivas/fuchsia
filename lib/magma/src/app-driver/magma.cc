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
#include "magma_device.h"
#include "magma_system.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

drm_intel_bo* magma_bo_alloc(drm_intel_bufmgr* bufmgr, const char* name, unsigned long size,
                             unsigned int alignment)
{
    DLOG("magma_bo_alloc %s size %ld alignment 0x%x", name, size, alignment);
    return bufmgr->AllocBufferObject(name, size, alignment, MAGMA_TILING_MODE_NONE, 0 /*stride*/);
}

drm_intel_bo* magma_bo_alloc_for_render(drm_intel_bufmgr* bufmgr, const char* name,
                                        unsigned long size, unsigned int alignment)
{
    UNIMPLEMENTED("magma_bo_alloc_for_render");
    return 0;
}

drm_intel_bo* magma_bo_alloc_tiled(drm_intel_bufmgr* bufmgr, const char* name, unsigned long size,
                                   unsigned long flags, uint32_t tiling_mode, unsigned long stride)
{
    DLOG("magma_bo_alloc_tiled %s size %lu flags 0x%lx tiling_mode 0x%x stride %lu", name, size,
         flags, tiling_mode, stride);
    // TODO: flags?
    return bufmgr->AllocBufferObject(name, size, 0 /*alignment*/, tiling_mode, stride);
}

int magma_bo_busy(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_bo_busy");
    return 0;
}

int magma_bo_emit_reloc(drm_intel_bo* bo, uint32_t offset, drm_intel_bo* target_bo,
                        uint32_t target_offset, uint32_t read_domains, uint32_t write_domain)
{
    DLOG("TODO magma_bo_emit_reloc - offset 0x%x target_offset 0x%x domains 0x%x 0x%x", offset,
         target_offset, read_domains, write_domain);
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
    DLOG("magma_bo_get_subdata '%s' STUB\n", MagmaBuffer::cast(bo)->Name());
    return 0;
}

int magma_bo_get_tiling(drm_intel_bo* bo, uint32_t* tiling_mode, uint32_t* swizzle_mode)
{
    DLOG("magma_bo_get_tiling - swizzle stubbed");
    auto buffer = MagmaBuffer::cast(bo);
    *tiling_mode = buffer->tiling_mode();
    // TODO: if swizzle_mode isn't used, remove it.
    *swizzle_mode = 0xdeadbeef;
    return 0;
}

int magma_bo_madvise(drm_intel_bo* bo, int madv)
{
    UNIMPLEMENTED("magma_bo_madvise");
    return 0;
}

int magma_bo_map(drm_intel_bo* bo, int write_enable)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_map %s", buffer->Name());
    buffer->Map(write_enable);
    return 0;
}

int magma_bo_mrb_exec(drm_intel_bo* bo, int used, void* unused, int num_cliprects, int DR4,
                      unsigned int flags)
{
    UNIMPLEMENTED("magma_bo_mrb_exec");
    return 0;
}

void magma_bo_reference(drm_intel_bo* bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_reference %s", buffer->Name());
    buffer->Incref();
}

int magma_bo_references(drm_intel_bo* bo, drm_intel_bo* target_bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    auto target_buffer = MagmaBuffer::cast(target_bo);
    DLOG("magma_bo_references bo %s target_bo %s", buffer->Name(), target_buffer->Name());
    return buffer->References(target_buffer);
    return 0;
}

int magma_bo_subdata(drm_intel_bo* bo, unsigned long offset, unsigned long size, const void* data)
{
    DLOG("magma_bo_subdata '%s' STUB", MagmaBuffer::cast(bo)->Name());
    return 0;
}

int magma_bo_unmap(drm_intel_bo* bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_unmap %p", bo);
    if (buffer)
        buffer->Unmap();
    return 0;
}

void magma_bo_unreference(drm_intel_bo* bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_unreference %s", buffer ? buffer->Name() : nullptr);
    if (buffer)
        buffer->Decref();
}

void magma_bo_wait_rendering(drm_intel_bo* bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_wait_rendering %s", buffer->Name());
    buffer->WaitRendering();
}

int magma_bufmgr_check_aperture_space(drm_intel_bo** bo_array, int count)
{
    DLOG("magma_bufmgr_check_aperture_space - STUB");
    return 0;
}

void magma_bufmgr_destroy(drm_intel_bufmgr* bufmgr)
{
    DLOG("magma_bufmgr_destroy");
    delete bufmgr;
}

void magma_bufmgr_gem_enable_fenced_relocs(drm_intel_bufmgr* bufmgr)
{
    DLOG("magma_bufmgr_gem_enable_fenced_relocs - STUB");
}

void magma_bufmgr_gem_enable_reuse(drm_intel_bufmgr* bufmgr)
{
    DLOG("magma_bufmgr_gem_enable_reuse - STUB");
}

int magma_bufmgr_gem_get_devid(drm_intel_bufmgr* bufmgr)
{
    DLOG("magma_bufmgr_gem_get_devid");
    int id = static_cast<int>(bufmgr->GetDeviceId());
    DLOG("returning id 0x%x", id);
    return id;
}

drm_intel_bufmgr* magma_bufmgr_gem_init(int device_handle, int batch_size)
{
    DLOG("magma_bufmgr_gem_init device_handle 0x%x batch_size %d", device_handle, batch_size);

    auto bufmgr = MagmaDevice::Open(device_handle, batch_size);
    if (!bufmgr) {
        DLOG("Failed to open device");
        return nullptr;
    }
    return bufmgr;
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
    DLOG("magma_gem_bo_clear_relocs - STUB");
}

int magma_gem_bo_get_reloc_count(drm_intel_bo* bo)
{
    DLOG("magma_gem_bo_get_reloc_count - STUB");
    return 0;
}

int magma_gem_bo_map_gtt(drm_intel_bo* bo)
{
    UNIMPLEMENTED("magma_gem_bo_map_gtt");
    return 0;
}

int magma_gem_bo_map_unsynchronized(drm_intel_bo* bo)
{
    DLOG("magma_gem_bo_map_unsynchronized %s - using regular map", MagmaBuffer::cast(bo)->Name());
    const int write_enable = 1;
    return magma_bo_map(bo, write_enable);
}

int magma_gem_bo_wait(drm_intel_bo* bo, int64_t timeout_ns)
{
    UNIMPLEMENTED("magma_gem_bo_wait");
    return 0;
}

drm_intel_context* magma_gem_context_create(drm_intel_bufmgr* bufmgr)
{
    DLOG("magma_gem_context_create");
    int context_id;
    if (bufmgr->CreateContext(&context_id)) {
        DLOG("got hw context id %d", context_id);
        return reinterpret_cast<drm_intel_context*>(context_id);
    }
    return nullptr;
}

void magma_gem_context_destroy(drm_intel_context* ctx) { DLOG("magma_gem_context_destroy - STUB"); }

int magma_gem_bo_context_exec(drm_intel_bo* bo, drm_intel_context* ctx, int used,
                              unsigned int flags)
{
    auto buffer = MagmaBuffer::cast(bo);
    int context_id = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    DLOG("magma_gem_bo_context_exec buffer '%s' context_id %d", buffer->Name(), context_id);
    // int int_context_id = static_cast<int>(context_id);
    buffer->device()->ExecuteBuffer(buffer, context_id, used, flags);
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
    DLOG("magma_reg_read - STUB returning 0");
    return 0;
}
