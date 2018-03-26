// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef void (*zx_intel_gpu_core_interrupt_callback_t)(void* data,
                                                       uint32_t master_interrupt_control);

typedef struct zx_intel_gpu_core_protocol_ops {
    // Reads 16 bits from pci config space; returned in |value_out|.
    zx_status_t (*read_pci_config_16)(void* ctx, uint64_t addr, uint16_t* value_out);

    // Maps the given |pci_bar|; address returned in |addr_out|, size in bytes returned in
    // |size_out|.
    zx_status_t (*map_pci_mmio)(void* ctx, uint32_t pci_bar, void** addr_out, uint64_t* size_out);

    // Unmaps the given |pci_bar|.
    zx_status_t (*unmap_pci_mmio)(void* ctx, uint32_t pci_bar);

    // Returns a bus transaction initiator.
    zx_status_t (*get_pci_bti)(void* ctx, uint32_t index, zx_handle_t* bti_out);

    // Registers the given |callback| to be invoked with parameter |data| when an interrupt occurs
    // matching |interrupt_mask|.
    zx_status_t (*register_interrupt_callback)(void* ctx,
                                               zx_intel_gpu_core_interrupt_callback_t callback,
                                               void* data, uint32_t interrupt_mask);

    // Un-registers a previously registered interrupt callback.
    zx_status_t (*unregister_interrupt_callback)(void* ctx);

    // Returns the size of the GTT (global translation table) in bytes.
    uint64_t (*gtt_get_size)(void* ctx);

    // Allocates a region of the GTT of the given |page_count|, returning the page-aligned virtual
    // address in |addr_out|.
    zx_status_t (*gtt_alloc)(void* ctx, uint64_t page_count, uint64_t* addr_out);

    // Frees the GTT allocation given by |addr|.
    zx_status_t (*gtt_free)(void* ctx, uint64_t addr);

    // Clears the page table entries for the GTT allocation given by |addr|.
    zx_status_t (*gtt_clear)(void* ctx, uint64_t addr);

    // Inserts page tables entries for the GTT allocation given by |addr| for the vmo represented by
    // handle |buffer|, at the given |page_offset| and |page_count|. Takes ownership of |buffer|.
    zx_status_t (*gtt_insert)(void* ctx, uint64_t addr, zx_handle_t buffer, uint64_t page_offset,
                              uint64_t page_count);

} zx_intel_gpu_core_protocol_ops_t;

typedef struct zx_intel_gpu_core_protocol {
    zx_intel_gpu_core_protocol_ops_t* ops;
    void* ctx;
} zx_intel_gpu_core_protocol_t;

__END_CDECLS;
