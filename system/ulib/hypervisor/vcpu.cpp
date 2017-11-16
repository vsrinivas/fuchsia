// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/vcpu.h>

#include <stdio.h>
#include <string.h>

#include <hypervisor/decode.h>
#include <hypervisor/io.h>
#include <hypervisor/guest.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

/* Interrupt vectors. */
#define X86_INT_GP_FAULT 13u

static zx_status_t unhandled_mem(const zx_packet_guest_mem_t* mem, const instruction_t* inst) {
    fprintf(stderr, "Unhandled address %#lx\n", mem->addr);
    if (inst->type == INST_MOV_READ)
        *inst->reg = UINT64_MAX;
    return ZX_OK;
}

static zx_status_t handle_mmio_read(vcpu_ctx_t* vcpu_ctx, uint64_t trap_key, zx_vaddr_t addr,
                                    uint8_t access_size, zx_vcpu_io_t* io) {
    IoValue value = {};
    value.access_size = access_size;
    zx_status_t status = trap_key_to_mapping(trap_key)->Read(addr, &value);
    if (status != ZX_OK)
        return status;

    io->access_size = value.access_size;
    io->u32 = value.u32;
    return ZX_OK;
}

static zx_status_t handle_mmio_write(vcpu_ctx_t* vcpu_ctx, uint64_t trap_key, zx_vaddr_t addr,
                                     zx_vcpu_io_t* io) {
    IoValue value;
    value.access_size = io->access_size;
    value.u32 = io->u32;
    return trap_key_to_mapping(trap_key)->Write(addr, value);
}

static zx_status_t handle_mmio(vcpu_ctx_t* vcpu_ctx, const zx_packet_guest_mem_t* mem,
                               uint64_t trap_key, const instruction_t* inst) {
    zx_status_t status;
    zx_vcpu_io_t mmio;
    if (inst->type == INST_MOV_WRITE) {
        switch (inst->access_size) {
        case 1:
            status = inst_write8(inst, &mmio.u8);
            break;
        case 2:
            status = inst_write16(inst, &mmio.u16);
            break;
        case 4:
            status = inst_write32(inst, &mmio.u32);
            break;
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }
        if (status != ZX_OK)
            return status;
        mmio.access_size = inst->access_size;
        return handle_mmio_write(vcpu_ctx, trap_key, mem->addr, &mmio);
    }

    if (inst->type == INST_MOV_READ) {
        status = handle_mmio_read(vcpu_ctx, trap_key, mem->addr, inst->access_size, &mmio);
        if (status != ZX_OK)
            return status;
        switch (inst->access_size) {
        case 1:
            return inst_read8(inst, mmio.u8);
        case 2:
            return inst_read16(inst, mmio.u16);
        case 4:
            return inst_read32(inst, mmio.u32);
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }
    }

    if (inst->type == INST_TEST) {
        status = handle_mmio_read(vcpu_ctx, trap_key, mem->addr, inst->access_size, &mmio);
        if (status != ZX_OK)
            return status;
        switch (inst->access_size) {
        case 1:
            return inst_test8(inst, static_cast<uint8_t>(inst->imm), mmio.u8);
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }
    }

    return ZX_ERR_INVALID_ARGS;
}

static zx_status_t handle_mem(vcpu_ctx_t* vcpu_ctx, const zx_packet_guest_mem_t* mem,
                              uint64_t trap_key) {
    zx_vcpu_state_t vcpu_state;
    zx_status_t status = vcpu_ctx->read_state(vcpu_ctx, ZX_VCPU_STATE, &vcpu_state,
                                              sizeof(vcpu_state));
    if (status != ZX_OK)
        return status;

    instruction_t inst;
#if __x86_64__
    status = inst_decode(mem->inst_buf, mem->inst_len, &vcpu_state, &inst);
#else
    status = ZX_ERR_NOT_SUPPORTED;
#endif

    if (status != ZX_OK) {
        fprintf(stderr, "Unsupported instruction:");
#if __x86_64__
        for (uint8_t i = 0; i < mem->inst_len; i++)
            fprintf(stderr, " %x", mem->inst_buf[i]);
#endif // __x86_64__
        fprintf(stderr, "\n");
    } else {
        status = handle_mmio(vcpu_ctx, mem, trap_key, &inst);
        if (status == ZX_ERR_NOT_FOUND)
            status = unhandled_mem(mem, &inst);
    }

    if (status != ZX_OK) {
        return zx_vcpu_interrupt(vcpu_ctx->vcpu, X86_INT_GP_FAULT);
    } else if (inst.type == INST_MOV_READ || inst.type == INST_TEST) {
        // If there was an attempt to read or test memory, update the GPRs.
        return vcpu_ctx->write_state(vcpu_ctx, ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    }
    return status;
}

static zx_status_t handle_input(vcpu_ctx_t* vcpu_ctx, const zx_packet_guest_io_t* io,
                                uint64_t trap_key) {
    IoValue value = {};
    value.access_size = io->access_size;
    zx_status_t status = trap_key_to_mapping(trap_key)->Read(io->port, &value);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to handle port in %#x: %d\n", io->port, status);
        return status;
    }

    zx_vcpu_io_t vcpu_io;
    memset(&vcpu_io, 0, sizeof(vcpu_io));
    vcpu_io.access_size = value.access_size;
    vcpu_io.u32 = value.u32;
    if (vcpu_io.access_size != io->access_size) {
        fprintf(stderr, "Unexpected size (%u != %u) for port in %#x\n", vcpu_io.access_size,
                io->access_size, io->port);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    return vcpu_ctx->write_state(vcpu_ctx, ZX_VCPU_IO, &vcpu_io, sizeof(vcpu_io));
}

static zx_status_t handle_output(vcpu_ctx_t* vcpu_ctx, const zx_packet_guest_io_t* io,
                                 uint64_t trap_key) {
    IoValue value;
    value.access_size = io->access_size;
    value.u32 = io->u32;
    zx_status_t status = trap_key_to_mapping(trap_key)->Write(io->port, value);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to handle port out %#x: %d\n", io->port, status);
    }
    return status;
}

static zx_status_t handle_io(vcpu_ctx_t* vcpu_ctx, const zx_packet_guest_io_t* io,
                             uint64_t trap_key) {
    return io->input ? handle_input(vcpu_ctx, io, trap_key) : handle_output(vcpu_ctx, io, trap_key);
}

static zx_status_t vcpu_state_read(vcpu_ctx_t* vcpu_ctx, uint32_t kind, void* buffer,
                                   uint32_t len) {
    return zx_vcpu_read_state(vcpu_ctx->vcpu, kind, buffer, len);
}

static zx_status_t vcpu_state_write(vcpu_ctx_t* vcpu_ctx, uint32_t kind, const void* buffer,
                                    uint32_t len) {
    return zx_vcpu_write_state(vcpu_ctx->vcpu, kind, buffer, len);
}

vcpu_ctx::vcpu_ctx(zx_handle_t vcpu_)
    : vcpu(vcpu_), read_state(&vcpu_state_read), write_state(vcpu_state_write) {}

zx_status_t vcpu_loop(vcpu_ctx_t* vcpu_ctx) {
    while (true) {
        zx_port_packet_t packet;
        zx_status_t status = zx_vcpu_resume(vcpu_ctx->vcpu, &packet);
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to resume VCPU %d\n", status);
            return status;
        }
        status = vcpu_packet_handler(vcpu_ctx, &packet);
        if (status == ZX_ERR_STOP)
            return ZX_OK;
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to handle guest packet %d: %d\n", packet.type, status);
            return status;
        }
    }
}

zx_status_t vcpu_packet_handler(vcpu_ctx_t* vcpu_ctx, zx_port_packet_t* packet) {
    switch (packet->type) {
    case ZX_PKT_TYPE_GUEST_MEM:
        return handle_mem(vcpu_ctx, &packet->guest_mem, packet->key);
#if __x86_64__
    case ZX_PKT_TYPE_GUEST_IO:
        return handle_io(vcpu_ctx, &packet->guest_io, packet->key);
#endif // __x86_64__
    default:
        fprintf(stderr, "Unhandled guest packet %d\n", packet->type);
        return ZX_ERR_NOT_SUPPORTED;
    }
}
