// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_DISPATCHER_H_

#include <sys/types.h>

#include <cstddef>

#include <dev/interrupt.h>
#include <fbl/bitfield.h>
#include <fbl/canary.h>
#include <fbl/ref_ptr.h>
#include <object/handle.h>
#include <object/interrupt_dispatcher.h>
#include <object/msi_allocation.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>

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

// Since the dispatcher only needs access to the Id register and Mask Bits register
// we are fortunately able to ignore the different formats due to the 32 bit and 64
// bit mask registers lining up.
// PCI Local Bus Specification rev 3.0 figure 6-9.

const uint8_t kMsiCapabilityId = (0x5);
const uint8_t kMsiXCapabilityId = (0x11);
const uint16_t kMsi64bitSupported = (1u << 7);
const uint16_t kMsiPvmSupported = (1u << 8);
struct MsiCapability {
  uint8_t id;
  uint8_t reserved0_;  // Next pointer
  uint16_t control;
  uint64_t reserved1_;    // For 32 bit this is Address, Data, and a reserved field.
                          // For 64 bit this is Address and Address Upper.
  uint32_t mask_bits_32;  // For 64 bit this is Data and a reserved field.
  uint32_t mask_bits_64;
  uint32_t reserved2_;  // Pending Bits
} __PACKED;
static_assert(offsetof(MsiCapability, mask_bits_32) == 0x0C);
static_assert(offsetof(MsiCapability, mask_bits_64) == 0x10);
static_assert(sizeof(MsiCapability) == 24);

// The common interface for all MSI related interrupt handling. This encompasses MSI and MSI-X.
class MsiDispatcher : public InterruptDispatcher {
 public:
  using RegisterIntFn = void (*)(const msi_block_t*, uint, int_handler, void*);

  static zx_status_t Create(fbl::RefPtr<MsiAllocation> alloc, uint32_t msi_id,
                            const fbl::RefPtr<VmObject>& vmo, zx_off_t cap_offset, uint32_t options,
                            zx_rights_t* out_rights,
                            KernelHandle<InterruptDispatcher>* out_interrupt,
                            RegisterIntFn = msi_register_handler);
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
                             bool has_cap_pvm, bool has_64bit, RegisterIntFn register_int_fn)
      : MsiDispatcher(ktl::move(alloc), ktl::move(mapping), base_irq_id, msi_id, register_int_fn),
        has_platform_pvm_(msi_supports_masking()),
        has_cap_pvm_(has_cap_pvm),
        capability_(reinterpret_cast<MsiCapability*>(this->mapping()->base() + reg_offset)),
        mask_bits_reg_((has_64bit) ? &capability_->mask_bits_64 : &capability_->mask_bits_32) {}
  void MaskInterrupt() final;
  void UnmaskInterrupt() final;

 private:
  // Not all interrupt controllers / configurations support masking at the
  // platform level. This is set accordingly if support is detected.
  const bool has_platform_pvm_;
  // Whether or not the given device function supports per vector masking within
  // the PCI MSI capability.
  const bool has_cap_pvm_;
  // A pointer to the MSI Mask Register for this specific device function. To
  // synchronize with other MSI interactions in the device it requires the MSI
  // allocation's lock.
  volatile MsiCapability* const capability_ TA_GUARDED(allocation()->lock()) = nullptr;
  volatile mutable uint32_t* mask_bits_reg_ TA_GUARDED(allocation()->lock()) = nullptr;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_DISPATCHER_H_
