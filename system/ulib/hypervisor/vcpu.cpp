// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/vcpu.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <hypervisor/decode.h>
#include <hypervisor/io.h>
#include <hypervisor/guest.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

// Interrupt vectors.
// TODO(abdulla): Pick the right vector for ARM.
static const uint32_t kGpFaultVector = 13u;

#if __aarch64__
static zx_status_t handle_mmio_arm(const zx_packet_guest_mem_t* mem, uint64_t trap_key,
                                   uint64_t* reg) {
    IoValue mmio = { mem->access_size, { .u64 = mem->data } };
    if (!mem->read)
        return trap_key_to_mapping(trap_key)->Write(mem->addr, mmio);

    zx_status_t status = trap_key_to_mapping(trap_key)->Read(mem->addr, &mmio);
    if (status != ZX_OK)
        return status;
    *reg = mmio.u64;
    if (mem->sign_extend && *reg & (1ul << (mmio.access_size * CHAR_BIT - 1)))
        *reg |= UINT64_MAX << mmio.access_size;
    return ZX_OK;
}
#elif __x86_64__
static zx_status_t handle_mmio_x86(const zx_packet_guest_mem_t* mem, uint64_t trap_key,
                                   const instruction_t* inst) {
    zx_status_t status;
    IoValue mmio = { inst->access_size, { .u64 = 0 } };
    switch (inst->type) {
    case INST_MOV_WRITE:
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
        return trap_key_to_mapping(trap_key)->Write(mem->addr, mmio);

    case INST_MOV_READ:
        status = trap_key_to_mapping(trap_key)->Read(mem->addr, &mmio);
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

    case INST_TEST:
        status = trap_key_to_mapping(trap_key)->Read(mem->addr, &mmio);
        if (status != ZX_OK)
            return status;
        switch (inst->access_size) {
        case 1:
            return inst_test8(inst, static_cast<uint8_t>(inst->imm), mmio.u8);
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }

    default:
        return ZX_ERR_INVALID_ARGS;
    }
}
#endif

static zx_status_t handle_mem(vcpu_ctx_t* vcpu_ctx, const zx_packet_guest_mem_t* mem,
                              uint64_t trap_key) {
    zx_vcpu_state_t vcpu_state;
    zx_status_t status;
#if __aarch64__
    if (mem->read)
#endif
    {
        status = vcpu_ctx->read_state(vcpu_ctx, ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
        if (status != ZX_OK)
            return status;
    }

    bool do_write;
#if __aarch64__
    do_write = mem->read;
    status = handle_mmio_arm(mem, trap_key, &vcpu_state.x[mem->xt]);
#elif __x86_64__
    instruction_t inst;
    status = inst_decode(mem->inst_buf, mem->inst_len, &vcpu_state, &inst);
    if (status != ZX_OK) {
        fprintf(stderr, "Unsupported instruction:");
        for (uint8_t i = 0; i < mem->inst_len; i++)
            fprintf(stderr, " %x", mem->inst_buf[i]);
        fprintf(stderr, "\n");
    } else {
        status = handle_mmio_x86(mem, trap_key, &inst);
        // If there was an attempt to read or test memory, update the GPRs.
        do_write = inst.type == INST_MOV_READ || inst.type == INST_TEST;
    }
#endif // __x86_64__

    if (status != ZX_OK) {
        return zx_vcpu_interrupt(vcpu_ctx->vcpu, kGpFaultVector);
    } else if (do_write) {
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
