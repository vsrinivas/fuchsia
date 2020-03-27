// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/vcpu_dispatcher.h"

#include <lib/counters.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <arch/hypervisor.h>
#include <fbl/alloc_checker.h>
#include <hypervisor/guest_physical_address_space.h>
#include <object/guest_dispatcher.h>
#include <vm/vm_object.h>

KCOUNTER(dispatcher_vcpu_create_count, "dispatcher.vcpu.create")
KCOUNTER(dispatcher_vcpu_destroy_count, "dispatcher.vcpu.destroy")

zx_status_t VcpuDispatcher::Create(fbl::RefPtr<GuestDispatcher> guest_dispatcher, zx_vaddr_t entry,
                                   KernelHandle<VcpuDispatcher>* handle, zx_rights_t* rights) {
  Guest* guest = guest_dispatcher->guest();

  ktl::unique_ptr<Vcpu> vcpu;
  zx_status_t status = Vcpu::Create(guest, entry, &vcpu);
  if (status != ZX_OK)
    return status;

  fbl::AllocChecker ac;
  KernelHandle new_handle(
      fbl::AdoptRef(new (&ac) VcpuDispatcher(guest_dispatcher, ktl::move(vcpu))));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

VcpuDispatcher::VcpuDispatcher(fbl::RefPtr<GuestDispatcher> guest, ktl::unique_ptr<Vcpu> vcpu)
    : guest_(guest), vcpu_(ktl::move(vcpu)) {
  kcounter_add(dispatcher_vcpu_create_count, 1);
}

VcpuDispatcher::~VcpuDispatcher() { kcounter_add(dispatcher_vcpu_destroy_count, 1); }

zx_status_t VcpuDispatcher::Resume(zx_port_packet_t* packet) {
  canary_.Assert();
  return vcpu_->Resume(packet);
}

void VcpuDispatcher::PhysicalInterrupt(uint32_t vector) {
  canary_.Assert();
  vcpu_->Interrupt(vector, hypervisor::InterruptType::PHYSICAL);
}

void VcpuDispatcher::VirtualInterrupt(uint32_t vector) {
  canary_.Assert();
  vcpu_->Interrupt(vector, hypervisor::InterruptType::VIRTUAL);
}

zx_status_t VcpuDispatcher::ReadState(zx_vcpu_state_t* vcpu_state) const {
  canary_.Assert();
  return vcpu_->ReadState(vcpu_state);
}

zx_status_t VcpuDispatcher::WriteState(const zx_vcpu_state_t& vcpu_state) {
  canary_.Assert();
  return vcpu_->WriteState(vcpu_state);
}

zx_status_t VcpuDispatcher::WriteState(const zx_vcpu_io_t& io_state) {
  canary_.Assert();
  return vcpu_->WriteState(io_state);
}
