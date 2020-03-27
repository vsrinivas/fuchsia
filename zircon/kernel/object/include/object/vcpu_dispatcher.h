// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_VCPU_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_VCPU_DISPATCHER_H_

#include <zircon/rights.h>
#include <zircon/syscalls/hypervisor.h>

#include <object/dispatcher.h>
#include <object/handle.h>

class GuestDispatcher;
class Vcpu;
class VmObject;

typedef struct zx_port_packet zx_port_packet_t;

class VcpuDispatcher final : public SoloDispatcher<VcpuDispatcher, ZX_DEFAULT_VCPU_RIGHTS> {
 public:
  static zx_status_t Create(fbl::RefPtr<GuestDispatcher> guest_dispatcher, zx_vaddr_t entry,
                            KernelHandle<VcpuDispatcher>* handle, zx_rights_t* rights);
  ~VcpuDispatcher();

  zx_obj_type_t get_type() const { return ZX_OBJ_TYPE_VCPU; }

  zx_status_t Resume(zx_port_packet_t* packet);
  void PhysicalInterrupt(uint32_t vector);
  void VirtualInterrupt(uint32_t vector);
  zx_status_t ReadState(zx_vcpu_state_t* vcpu_state) const;
  zx_status_t WriteState(const zx_vcpu_state_t& vcpu_state);
  zx_status_t WriteState(const zx_vcpu_io_t& io_state);

 private:
  fbl::RefPtr<GuestDispatcher> guest_;
  ktl::unique_ptr<Vcpu> vcpu_;

  explicit VcpuDispatcher(fbl::RefPtr<GuestDispatcher> guest, ktl::unique_ptr<Vcpu> vcpu);
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_VCPU_DISPATCHER_H_
