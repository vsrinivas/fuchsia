// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_gpu_core.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct zx_intel_gpu_core_interrupt zx_intel_gpu_core_interrupt_t;
typedef struct zx_intel_gpu_core_protocol zx_intel_gpu_core_protocol_t;

// Declarations

#define IMAGE_TYPE_Y_LEGACY_TILED UINT32_C(2)

struct zx_intel_gpu_core_interrupt {
    void (*callback)(void* ctx, uint32_t master_interrupt_control);
    void* ctx;
};

typedef struct zx_intel_gpu_core_protocol_ops {
    zx_status_t (*read_pci_config16)(void* ctx, uint16_t addr, uint16_t* out_value);
    zx_status_t (*map_pci_mmio)(void* ctx, uint32_t pci_bar, void** out_buf_buffer,
                                size_t* buf_size);
    zx_status_t (*unmap_pci_mmio)(void* ctx, uint32_t pci_bar);
    zx_status_t (*get_pci_bti)(void* ctx, uint32_t index, zx_handle_t* out_bti);
    zx_status_t (*register_interrupt_callback)(void* ctx,
                                               const zx_intel_gpu_core_interrupt_t* callback,
                                               uint32_t interrupt_mask);
    zx_status_t (*unregister_interrupt_callback)(void* ctx);
    uint64_t (*gtt_get_size)(void* ctx);
    zx_status_t (*gtt_alloc)(void* ctx, uint64_t page_count, uint64_t* out_addr);
    zx_status_t (*gtt_free)(void* ctx, uint64_t addr);
    zx_status_t (*gtt_clear)(void* ctx, uint64_t addr);
    zx_status_t (*gtt_insert)(void* ctx, uint64_t addr, zx_handle_t buffer, uint64_t page_offset,
                              uint64_t page_count);
} zx_intel_gpu_core_protocol_ops_t;

struct zx_intel_gpu_core_protocol {
    zx_intel_gpu_core_protocol_ops_t* ops;
    void* ctx;
};

// Reads 16 bits from pci config space; returned in |value_out|.
static inline zx_status_t
zx_intel_gpu_core_read_pci_config16(const zx_intel_gpu_core_protocol_t* proto, uint16_t addr,
                                    uint16_t* out_value) {
    return proto->ops->read_pci_config16(proto->ctx, addr, out_value);
}
// Maps the given |pci_bar|; address returned in |addr_out|, size in bytes returned in
// |size_out|.
static inline zx_status_t zx_intel_gpu_core_map_pci_mmio(const zx_intel_gpu_core_protocol_t* proto,
                                                         uint32_t pci_bar, void** out_buf_buffer,
                                                         size_t* buf_size) {
    return proto->ops->map_pci_mmio(proto->ctx, pci_bar, out_buf_buffer, buf_size);
}
// Unmaps the given |pci_bar|.
static inline zx_status_t
zx_intel_gpu_core_unmap_pci_mmio(const zx_intel_gpu_core_protocol_t* proto, uint32_t pci_bar) {
    return proto->ops->unmap_pci_mmio(proto->ctx, pci_bar);
}
// Returns a bus transaction initiator.
static inline zx_status_t zx_intel_gpu_core_get_pci_bti(const zx_intel_gpu_core_protocol_t* proto,
                                                        uint32_t index, zx_handle_t* out_bti) {
    return proto->ops->get_pci_bti(proto->ctx, index, out_bti);
}
// Registers the given |callback| to be invoked with parameter |data| when an interrupt occurs
// matching |interrupt_mask|.
static inline zx_status_t
zx_intel_gpu_core_register_interrupt_callback(const zx_intel_gpu_core_protocol_t* proto,
                                              const zx_intel_gpu_core_interrupt_t* callback,
                                              uint32_t interrupt_mask) {
    return proto->ops->register_interrupt_callback(proto->ctx, callback, interrupt_mask);
}
// Un-registers a previously registered interrupt callback.
static inline zx_status_t
zx_intel_gpu_core_unregister_interrupt_callback(const zx_intel_gpu_core_protocol_t* proto) {
    return proto->ops->unregister_interrupt_callback(proto->ctx);
}
// Returns the size of the GTT (global translation table) in bytes.
static inline uint64_t zx_intel_gpu_core_gtt_get_size(const zx_intel_gpu_core_protocol_t* proto) {
    return proto->ops->gtt_get_size(proto->ctx);
}
// Allocates a region of the GTT of the given |page_count|, returning the page-aligned virtual
// address in |addr_out|.
static inline zx_status_t zx_intel_gpu_core_gtt_alloc(const zx_intel_gpu_core_protocol_t* proto,
                                                      uint64_t page_count, uint64_t* out_addr) {
    return proto->ops->gtt_alloc(proto->ctx, page_count, out_addr);
}
// Frees the GTT allocation given by |addr|.
static inline zx_status_t zx_intel_gpu_core_gtt_free(const zx_intel_gpu_core_protocol_t* proto,
                                                     uint64_t addr) {
    return proto->ops->gtt_free(proto->ctx, addr);
}
// Clears the page table entries for the GTT allocation given by |addr|.
static inline zx_status_t zx_intel_gpu_core_gtt_clear(const zx_intel_gpu_core_protocol_t* proto,
                                                      uint64_t addr) {
    return proto->ops->gtt_clear(proto->ctx, addr);
}
// Inserts page tables entries for the GTT allocation given by |addr| for the vmo represented by
// handle |buffer|, at the given |page_offset| and |page_count|. Takes ownership of |buffer|.
static inline zx_status_t zx_intel_gpu_core_gtt_insert(const zx_intel_gpu_core_protocol_t* proto,
                                                       uint64_t addr, zx_handle_t buffer,
                                                       uint64_t page_offset, uint64_t page_count) {
    return proto->ops->gtt_insert(proto->ctx, addr, buffer, page_offset, page_count);
}

#define IMAGE_TYPE_YF_TILED UINT32_C(3)

#define IMAGE_TYPE_X_TILED UINT32_C(1)

__END_CDECLS;
