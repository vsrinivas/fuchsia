// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_INTERRUPT_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_INTERRUPT_DISPATCHER_H_

#include <sys/types.h>

#include <fbl/canary.h>
#include <fbl/ref_ptr.h>
#include <object/handle.h>
#include <object/interrupt_dispatcher.h>
#include <object/msi_allocation.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>

#include "dev/interrupt.h"

// Message Signaled Interrupts --
// This derived interrupt dispatcher handles operation of Messaged Signaled
// Interrupts (MSIs) and their associated interactions with userspace drivers.
// MSIs are allocated at the platform interrupt controller in contiguous blocks
// and then assigned as a group to a given PCI device. A PCI device may support
// 1 or more interrupts, and may or may not support masking of individual
// vectors. Operation of the MSI functionality is largely controlled in the
// device's capability space via its MSI Capability Structure. This includes
// enabling MSI, configuring vectors, and masking/unmasking vectors. To reduce
// interrupt latency all masking and unmasking at interrupt time is handled by
// this dispatcher, but all configuration is (will be: fxb/32978) handled by the
// userspace PCI Bus Driver. To facilitate safe interactions between the two,
// all access to MSI configuration registers are synchronized via the MSI
// allocation lock. Userspace will rarely be accessing this outside of
// initialization so the performance overhead is minimal due to a lack of
// congestion and interrupts all being handled by the bootstrap CPU. For the
// kernel PCI driver the lock is used directly with no extra consideration
// needed.

// A device supports Per-Vector-Masking
#define MSI_FLAG_HAS_PVM (1 << 0)

// The common interface for all MSI related interrupt handling. This encompasses MSI and MSI-X.
class MsiDispatcher : public InterruptDispatcher {
 public:
  using RegisterIntFn = void (*)(const msi_block_t*, uint, int_handler, void*);

  static zx_status_t Create(fbl::RefPtr<MsiAllocation> alloc, uint32_t msi_id,
                            const fbl::RefPtr<VmObject>& vmo, zx_off_t reg_offset, uint32_t flags,
                            zx_rights_t* out_rights,
                            KernelHandle<InterruptDispatcher>* out_interrupt,
                            RegisterIntFn = msi_register_handler, bool test_interrupt = false);
  ~MsiDispatcher() override;
  uint32_t msi_id() const { return msi_id_; }
  constexpr uint32_t vector() const { return base_irq_id_ + msi_id_; }
  void UnregisterInterruptHandler() final;
  zx_status_t RegisterInterruptHandler();

 protected:
  explicit MsiDispatcher(fbl::RefPtr<MsiAllocation>&& alloc, fbl::RefPtr<VmMapping>&& mapping,
                         uint32_t base_irq_id, uint32_t msi_id,
                         RegisterIntFn = msi_register_handler);
  fbl::RefPtr<VmMapping>& mapping() { return mapping_; }
  fbl::RefPtr<MsiAllocation>& allocation() { return alloc_; }
  static interrupt_eoi IrqHandler(void* ctx);

 private:
  // The MSI allocation block this dispatcher shares.
  fbl::RefPtr<MsiAllocation> alloc_;
  // The config space of the MSI capability controlling this MSI vector.
  fbl::RefPtr<VmMapping> mapping_;
  // A pointer to the function to register the msi interrupt handler. Allows
  // tests to override msi_register_handler.
  const RegisterIntFn register_int_fn_;
  // Cache the base irq id of the block so we can use it without locking.
  const uint32_t base_irq_id_ = 0;
  // The specific MSI id within the block that this dispatcher services.
  const uint32_t msi_id_ = 0;
};

// Each of the types of MSIs supported need their own Mask/Unmask based on
// constraints of the system. At this time MaskInterrupt/UnmaskInterrupt are
// virtual at the InterruptDispatcher level so we're accumulating no extra
// cost by making them virtual here.
class MsiDispatcherImpl : public MsiDispatcher {
 public:
  explicit MsiDispatcherImpl(fbl::RefPtr<MsiAllocation>&& alloc, uint32_t base_irq_id,
                             uint32_t msi_id, fbl::RefPtr<VmMapping>&& mapping, zx_off_t reg_offset,
                             bool capability_pvm_supported, RegisterIntFn register_int_fn)
      : MsiDispatcher(ktl::move(alloc), ktl::move(mapping), base_irq_id, msi_id, register_int_fn),
        mask_reg_(reinterpret_cast<uint32_t*>(this->mapping()->base() + reg_offset)),
        platform_pvm_supported_(msi_supports_masking()),
        capability_pvm_supported_(capability_pvm_supported) {}
  void MaskInterrupt() final;
  void UnmaskInterrupt() final;

 private:
  // A pointer to the MSI Mask Register for this specific device function. To
  // synchronize with other MSI interactions in the device it requires the MSI
  // allocation's lock.
  volatile uint32_t* const mask_reg_ TA_GUARDED(allocation()->lock()) = nullptr;
  // Not all interrupt controllers / configurations support masking at the
  // platform level. This is set accordingly if support is detected.
  const bool platform_pvm_supported_;
  // Whether or not the given device function supports per vector masking within
  // the PCI MSI capability.
  const bool capability_pvm_supported_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_INTERRUPT_DISPATCHER_H_
