// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "magma_connection.h"
#include "magma_system.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

magma_buffer* magma_bo_alloc(magma_connection* connection, const char* name, uint32_t size,
                             uint32_t alignment)
{
    DLOG("magma_bo_alloc %s size %ld alignment 0x%x", name, size, alignment);
    return MagmaConnection::cast(connection)
        ->AllocBufferObject(name, size, alignment, MAGMA_TILING_MODE_NONE, 0 /*stride*/);
}

magma_buffer* magma_bo_alloc_for_render(magma_connection* connection, const char* name,
                                        uint32_t size, uint32_t alignment)
{
    UNIMPLEMENTED("magma_bo_alloc_for_render");
    return 0;
}

magma_buffer* magma_bo_alloc_tiled(magma_connection* connection, const char* name, uint32_t size,
                                   uint32_t flags, uint32_t tiling_mode, uint32_t stride)
{
    DLOG("magma_bo_alloc_tiled %s size %lu flags 0x%lx tiling_mode 0x%x stride %lu", name, size,
         flags, tiling_mode, stride);
    // TODO: flags?
    return MagmaConnection::cast(connection)
        ->AllocBufferObject(name, size, 0 /*alignment*/, tiling_mode, stride);
}

int32_t magma_bo_busy(magma_buffer* bo)
{
    UNIMPLEMENTED("magma_bo_busy");
    return 0;
}

int32_t magma_bo_emit_reloc(magma_buffer* bo, uint32_t offset, magma_buffer* target_bo,
                            uint32_t target_offset, uint32_t read_domains, uint32_t write_domain)
{
    DLOG("TODO magma_bo_emit_reloc - offset 0x%x target_offset 0x%x domains 0x%x 0x%x", offset,
         target_offset, read_domains, write_domain);
    return 0;
}

int32_t magma_bo_flink(magma_buffer* bo, uint32_t* name)
{
    UNIMPLEMENTED("mamga_bo_flink");
    return 0;
}

magma_buffer* magma_bo_gem_create_from_name(magma_connection* connection, const char* name,
                                            uint32_t handle)
{
    UNIMPLEMENTED("magma_bo_gem_create_from_name");
    return 0;
}

magma_buffer* magma_bo_gem_create_from_prime(magma_connection* connection, int32_t prime_fd,
                                             int32_t size)
{
    UNIMPLEMENTED("magma_bo_gem_create_from_prime");
    return 0;
}

int32_t magma_bo_gem_export_to_prime(magma_buffer* bo, int32_t* prime_fd)
{
    UNIMPLEMENTED("magma_bo_gem_export_to_prime");
    return 0;
}

int32_t magma_bo_get_subdata(magma_buffer* bo, uint32_t offset, uint32_t size, void* data)
{
    DLOG("magma_bo_get_subdata '%s' STUB\n", MagmaBuffer::cast(bo)->Name());
    return 0;
}

int32_t magma_bo_get_tiling(magma_buffer* bo, uint32_t* tiling_mode, uint32_t* swizzle_mode)
{
    DLOG("magma_bo_get_tiling - swizzle stubbed");
    auto buffer = MagmaBuffer::cast(bo);
    *tiling_mode = buffer->tiling_mode();
    // TODO: if swizzle_mode isn't used, remove it.
    *swizzle_mode = 0xdeadbeef;
    return 0;
}

int32_t magma_bo_madvise(magma_buffer* bo, int32_t madv)
{
    UNIMPLEMENTED("magma_bo_madvise");
    return 0;
}

int32_t magma_bo_map(magma_buffer* bo, int32_t write_enable)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_map %s", buffer->Name());
    buffer->Map(write_enable);
    return 0;
}

int32_t magma_bo_mrb_exec(magma_buffer* bo, int32_t used, void* unused, int32_t num_cliprects,
                          int32_t DR4, uint32_t flags)
{
    UNIMPLEMENTED("magma_bo_mrb_exec");
    return 0;
}

void magma_bo_reference(magma_buffer* bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_reference %s", buffer->Name());
    buffer->Incref();
}

int32_t magma_bo_references(magma_buffer* bo, magma_buffer* target_bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    auto target_buffer = MagmaBuffer::cast(target_bo);
    DLOG("magma_bo_references bo %s target_bo %s", buffer->Name(), target_buffer->Name());
    return buffer->References(target_buffer);
    return 0;
}

int32_t magma_bo_subdata(magma_buffer* bo, uint32_t offset, uint32_t size, const void* data)
{
    DLOG("magma_bo_subdata '%s' STUB", MagmaBuffer::cast(bo)->Name());
    return 0;
}

int32_t magma_bo_unmap(magma_buffer* bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_unmap %p", bo);
    if (buffer)
        buffer->Unmap();
    return 0;
}

void magma_bo_unreference(magma_buffer* bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_unreference %s", buffer ? buffer->Name() : nullptr);
    if (buffer)
        buffer->Decref();
}

void magma_bo_wait_rendering(magma_buffer* bo)
{
    auto buffer = MagmaBuffer::cast(bo);
    DLOG("magma_bo_wait_rendering %s", buffer->Name());
    buffer->WaitRendering();
}

int32_t magma_bufmgr_check_aperture_space(magma_buffer** bo_array, int32_t count)
{
    DLOG("magma_bufmgr_check_aperture_space - STUB");
    return 0;
}

void magma_bufmgr_destroy(magma_connection* connection)
{
    DLOG("magma_bufmgr_destroy");
    delete connection;
}

void magma_bufmgr_gem_enable_fenced_relocs(magma_connection* connection)
{
    DLOG("magma_bufmgr_gem_enable_fenced_relocs - STUB");
}

void magma_bufmgr_gem_enable_reuse(magma_connection* connection)
{
    DLOG("magma_bufmgr_gem_enable_reuse - STUB");
}

int32_t magma_bufmgr_gem_get_devid(magma_connection* connection)
{
    DLOG("magma_bufmgr_gem_get_devid");
    int32_t id = static_cast<int>(MagmaConnection::cast(connection)->GetDeviceId());
    DLOG("returning id 0x%x", id);
    return id;
}

magma_connection* magma_bufmgr_gem_init(int32_t connection_handle, int32_t batch_size)
{
    DLOG("magma_bufmgr_gem_init connection_handle 0x%x batch_size %d", connection_handle,
         batch_size);

    auto connection = MagmaConnection::Open(connection_handle, batch_size);
    if (!connection) {
        DLOG("Failed to open connection");
        return nullptr;
    }
    return connection;
}

void magma_bufmgr_gem_set_aub_annotations(magma_buffer* bo, drm_intel_aub_annotation* annotations,
                                          unsigned count)
{
    UNIMPLEMENTED("magma_bufmgr_gem_set_aub_annotations");
}

void magma_bufmgr_gem_set_aub_dump(magma_connection* connection, int32_t enable)
{
    UNIMPLEMENTED("magma_bufmgr_gem_set_aub_dump");
}

void magma_bufmgr_set_debug(magma_connection* connection, int32_t enable_debug)
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
                                    int32_t count)
{
    UNIMPLEMENTED("magma_decode_set_batch_pointer");
}

void magma_decode_set_output_file(struct drm_intel_decode* ctx, FILE* out)
{
    UNIMPLEMENTED("magma_decode_set_output_file");
}

void magma_gem_bo_aub_dump_bmp(magma_buffer* bo, int32_t x1, int32_t y1, int32_t width,
                               int32_t height, enum aub_dump_bmp_format format, int32_t pitch,
                               int32_t offset)
{
    UNIMPLEMENTED("magma_gem_bo_aub_dump_bmp");
}

void magma_gem_bo_clear_relocs(magma_buffer* bo, int32_t start)
{
    DLOG("magma_gem_bo_clear_relocs - STUB");
}

int32_t magma_gem_bo_get_reloc_count(magma_buffer* bo)
{
    DLOG("magma_gem_bo_get_reloc_count - STUB");
    return 0;
}

int32_t magma_gem_bo_map_gtt(magma_buffer* bo)
{
    UNIMPLEMENTED("magma_gem_bo_map_gtt");
    return 0;
}

int32_t magma_gem_bo_map_unsynchronized(magma_buffer* bo)
{
    DLOG("magma_gem_bo_map_unsynchronized %s - using regular map", MagmaBuffer::cast(bo)->Name());
    const int32_t write_enable = 1;
    return magma_bo_map(bo, write_enable);
}

int32_t magma_gem_bo_wait(magma_buffer* bo, int64_t timeout_ns)
{
    UNIMPLEMENTED("magma_gem_bo_wait");
    return 0;
}

magma_context* magma_gem_context_create(magma_connection* connection)
{
    DLOG("magma_gem_context_create");
    auto context = MagmaConnection::cast(connection)->CreateContext();
    if (!context)
        return DRETP(nullptr, "Could not create context");

    DLOG("got hw context id %d", context->context_id());
    return context;
}

void magma_gem_context_destroy(magma_context* ctx)
{
    auto context = MagmaContext::cast(ctx);
    if (!context->connection()->DestroyContext(context)) {
        DLOG("failed to destroy context. Somehow...");
    }
}

int32_t magma_gem_bo_context_exec(magma_buffer* bo, magma_context* ctx, int32_t used,
                                  uint32_t flags)
{
    auto buffer = MagmaBuffer::cast(bo);
    int32_t context_id = MagmaContext::cast(ctx)->context_id();
    DLOG("magma_gem_bo_context_exec buffer '%s' context_id %d", buffer->Name(), context_id);
    // int32_t int_context_id = static_cast<int>(context_id);
    buffer->connection()->ExecuteBuffer(buffer, context_id, used, flags);
    return 0;
}

int32_t magma_get_aperture_sizes(int32_t fd, size_t* mappable, size_t* total)
{
    UNIMPLEMENTED("magma_get_aperture_sizes");
    return 0;
}

int32_t magma_get_reset_stats(magma_context* ctx, uint32_t* reset_count, uint32_t* active,
                              uint32_t* pending)
{
    UNIMPLEMENTED("magma_get_reset_stats");
    return 0;
}

int32_t magma_reg_read(magma_connection* connection, uint32_t offset, uint64_t* result)
{
    DLOG("magma_reg_read - STUB returning 0");
    return 0;
}
