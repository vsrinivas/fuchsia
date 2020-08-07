// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/ktrace.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include <arch/hypervisor.h>
#include <arch/ops.h>
#include <fbl/auto_call.h>
#include <hypervisor/cpu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/ktrace.h>
#include <kernel/event.h>
#include <kernel/percpu.h>
#include <kernel/stats.h>
#include <platform/timer.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

zx_status_t Vcpu::Create(Guest* guest, zx_vaddr_t entry, ktl::unique_ptr<Vcpu>* out) {
  return ZX_OK;
}

Vcpu::Vcpu(Guest* guest, uint8_t vpid, const Thread* thread) {
}

Vcpu::~Vcpu() {
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
  return ZX_OK;
}

void Vcpu::Interrupt(uint32_t vector, hypervisor::InterruptType type) {
}

zx_status_t Vcpu::ReadState(zx_vcpu_state_t* state) const {
  return ZX_OK;
}

zx_status_t Vcpu::WriteState(const zx_vcpu_state_t& state) {
  return ZX_OK;
}
