// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <hypervisor/local_apic.h>
#include <zircon/syscalls/hypervisor.h>

class IoApic;
class PciBus;

typedef struct io_port io_port_t;
typedef struct zx_port_packet zx_port_packet_t;
typedef struct uart uart_t;

/* Stores the state associated with the guest. */
typedef struct guest_ctx {
    IoApic* io_apic;
    io_port_t* io_port;
    PciBus* pci_bus;

    uart_t* uart;
} guest_ctx_t;

/* Typedefs to abstract reading and writing VCPU state. */
typedef struct vcpu_ctx vcpu_ctx_t;
typedef zx_status_t (*read_state_fn_t)(vcpu_ctx_t* vcpu, uint32_t kind, void* buffer, uint32_t len);
typedef zx_status_t (*write_state_fn_t)(vcpu_ctx_t* vcpu, uint32_t kind, const void* buffer, uint32_t len);

/* Stores the state associated with a single VCPU. */
typedef struct vcpu_ctx {
    vcpu_ctx(zx_handle_t vcpu_, uintptr_t apic_addr_);
    vcpu_ctx(zx_handle_t vcpu_) : vcpu_ctx(vcpu_, 0) {}

    zx_handle_t vcpu;

    read_state_fn_t read_state;
    write_state_fn_t write_state;

    LocalApic local_apic;
    guest_ctx_t* guest_ctx = nullptr;
} vcpu_ctx_t;

/* Controls execution of a VCPU context, providing the main logic. */
zx_status_t vcpu_loop(vcpu_ctx_t* vcpu_ctx);

/* Processes a single guest packet. */
zx_status_t vcpu_packet_handler(vcpu_ctx_t* vcpu_ctx, zx_port_packet_t* packet);

typedef zx_status_t (*device_handler_fn_t)(zx_port_packet_t* packet, void* ctx);

/* A set of arguments to specify a trap region.
 *
 * See zx_guest_set_trap for more details on trap args.
 */
typedef struct trap_args {
    uint32_t kind;
    zx_vaddr_t addr;
    size_t len;
    uint32_t key;
    bool use_port;
} trap_args_t;

/* Set traps for a device and start asynchronous handling of device operations
 * for any traps where |trap_args_t::use_port| is true.
 */
zx_status_t device_trap(zx_handle_t guest, const trap_args_t* traps, size_t num_traps,
                        device_handler_fn_t handler, void* ctx);
