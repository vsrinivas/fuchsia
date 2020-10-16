// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <err.h>
#include <lib/user_copy/user_ptr.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/smc.h>

#include <new>

#include <dev/interrupt.h>
#include <dev/iommu.h>
#include <dev/udisplay.h>
#include <fbl/auto_call.h>
#include <fbl/inline_array.h>
#include <object/bus_transaction_initiator_dispatcher.h>
#include <object/handle.h>
#include <object/interrupt_dispatcher.h>
#include <object/interrupt_event_dispatcher.h>
#include <object/iommu_dispatcher.h>
#include <object/msi_allocation.h>
#include <object/msi_allocation_dispatcher.h>
#include <object/msi_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resource.h>
#include <object/vcpu_dispatcher.h>
#include <object/virtual_interrupt_dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>

#if ARCH_X86
#include <platform/pc/bootloader.h>
#include <platform/pc/smbios.h>
#endif

#include "ddk_priv.h"
#include "priv.h"

#define LOCAL_TRACE 0

// zx_status_t zx_vmo_create_contiguous
zx_status_t sys_vmo_create_contiguous(zx_handle_t bti, size_t size, uint32_t alignment_log2,
                                      user_out_handle* out) {
  LTRACEF("size 0x%zu\n", size);

  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (alignment_log2 == 0) {
    alignment_log2 = PAGE_SIZE_SHIFT;
  }
  // catch obviously wrong values
  if (alignment_log2 < PAGE_SIZE_SHIFT || alignment_log2 >= (8 * sizeof(uint64_t))) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t status = up->EnforceBasicPolicy(ZX_POL_NEW_VMO);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<BusTransactionInitiatorDispatcher> bti_dispatcher;
  status = up->handle_table().GetDispatcherWithRights(bti, ZX_RIGHT_MAP, &bti_dispatcher);
  if (status != ZX_OK) {
    return status;
  }

  auto align_log2_arg = static_cast<uint8_t>(alignment_log2);

  // create a vm object
  fbl::RefPtr<VmObjectPaged> vmo;
  status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, size, align_log2_arg, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  // create a Vm Object dispatcher
  KernelHandle<VmObjectDispatcher> kernel_handle;
  zx_rights_t rights;
  zx_status_t result = VmObjectDispatcher::Create(ktl::move(vmo), &kernel_handle, &rights);
  if (result != ZX_OK) {
    return result;
  }

  // create a handle and attach the dispatcher to it
  return out->make(ktl::move(kernel_handle), rights);
}

// zx_status_t zx_vmo_create_physical
zx_status_t sys_vmo_create_physical(zx_handle_t hrsrc, zx_paddr_t paddr, size_t size,
                                    user_out_handle* out) {
  LTRACEF("size 0x%zu\n", size);

  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t status = up->EnforceBasicPolicy(ZX_POL_NEW_VMO);
  if (status != ZX_OK) {
    return status;
  }

  // Memory should be subtracted from the PhysicalAspace allocators, so it's
  // safe to assume that if the caller has access to a resource for this specified
  // region of MMIO space then it is safe to allow the vmo to be created.
  if ((status = validate_resource_mmio(hrsrc, paddr, size)) != ZX_OK) {
    return status;
  }

  size = ROUNDUP_PAGE_SIZE(size);

  // create a vm object
  fbl::RefPtr<VmObjectPhysical> vmo;
  zx_status_t result = VmObjectPhysical::Create(paddr, size, &vmo);
  if (result != ZX_OK) {
    return result;
  }

  // create a Vm Object dispatcher
  KernelHandle<VmObjectDispatcher> kernel_handle;
  zx_rights_t rights;
  result = VmObjectDispatcher::Create(ktl::move(vmo), &kernel_handle, &rights);
  if (result != ZX_OK) {
    return result;
  }

  // create a handle and attach the dispatcher to it
  return out->make(ktl::move(kernel_handle), rights);
}

// zx_status_t zx_framebuffer_get_info
zx_status_t sys_framebuffer_get_info(zx_handle_t handle, user_out_ptr<uint32_t> format,
                                     user_out_ptr<uint32_t> width, user_out_ptr<uint32_t> height,
                                     user_out_ptr<uint32_t> stride) {
  zx_status_t status;
  if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
    return status;
  }
#if ARCH_X86
  if (!bootloader.fb.base) {
    return ZX_ERR_INVALID_ARGS;
  }
  status = format.copy_to_user(bootloader.fb.format);
  if (status != ZX_OK) {
    return status;
  }
  status = width.copy_to_user(bootloader.fb.width);
  if (status != ZX_OK) {
    return status;
  }
  status = height.copy_to_user(bootloader.fb.height);
  if (status != ZX_OK) {
    return status;
  }
  status = stride.copy_to_user(bootloader.fb.stride);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
#else
  return ZX_ERR_NOT_SUPPORTED;
#endif
}

// zx_status_t zx_framebuffer_set_range
zx_status_t sys_framebuffer_set_range(zx_handle_t hrsrc, zx_handle_t vmo_handle, uint32_t len,
                                      uint32_t format, uint32_t width, uint32_t height,
                                      uint32_t stride) {
  zx_status_t status;
  if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
    return status;
  }

  if (vmo_handle == ZX_HANDLE_INVALID) {
    udisplay_clear_framebuffer_vmo();
    return ZX_OK;
  }

  auto up = ProcessDispatcher::GetCurrent();

  // lookup the dispatcher from handle
  fbl::RefPtr<VmObjectDispatcher> vmo;
  status = up->handle_table().GetDispatcher(vmo_handle, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  status = udisplay_set_framebuffer(vmo->vmo());
  if (status != ZX_OK) {
    return status;
  }

  struct display_info di;
  memset(&di, 0, sizeof(struct display_info));
  di.format = format;
  di.width = width;
  di.height = height;
  di.stride = stride;
  di.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
  udisplay_set_display_info(&di);

  return ZX_OK;
}

// zx_status_t zx_iommu_create
zx_status_t sys_iommu_create(zx_handle_t resource, uint32_t type, user_in_ptr<const void> desc,
                             size_t desc_size, user_out_handle* out) {
  // TODO: finer grained validation
  zx_status_t status;
  if ((status = validate_resource(resource, ZX_RSRC_KIND_ROOT)) < 0) {
    return status;
  }

  if (desc_size > ZX_IOMMU_MAX_DESC_LEN) {
    return ZX_ERR_INVALID_ARGS;
  }

  KernelHandle<IommuDispatcher> handle;
  zx_rights_t rights;

  {
    // Copy the descriptor into the kernel and try to create the dispatcher
    // using it.
    fbl::AllocChecker ac;
    ktl::unique_ptr<uint8_t[]> copied_desc(new (&ac) uint8_t[desc_size]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    if ((status = desc.reinterpret<const uint8_t>().copy_array_from_user(copied_desc.get(),
                                                                         desc_size)) != ZX_OK) {
      return status;
    }
    status = IommuDispatcher::Create(type, ktl::unique_ptr<const uint8_t[]>(copied_desc.release()),
                                     desc_size, &handle, &rights);
    if (status != ZX_OK) {
      return status;
    }
  }

  return out->make(ktl::move(handle), rights);
}

#if ARCH_X86
#include <arch/x86/descriptor.h>
#include <arch/x86/ioport.h>

// zx_status_t zx_ioports_request
zx_status_t sys_ioports_request(zx_handle_t hrsrc, uint16_t io_addr, uint32_t len) {
  zx_status_t status;
  if ((status = validate_resource_ioport(hrsrc, io_addr, len)) != ZX_OK) {
    return status;
  }

  LTRACEF("addr 0x%x len 0x%x\n", io_addr, len);

  return IoBitmap::GetCurrent()->SetIoBitmap(io_addr, len, /*enable=*/true);
}

// zx_status_t zx_ioports_release
zx_status_t sys_ioports_release(zx_handle_t hrsrc, uint16_t io_addr, uint32_t len) {
  zx_status_t status;
  if ((status = validate_resource_ioport(hrsrc, io_addr, len)) != ZX_OK) {
    return status;
  }

  LTRACEF("addr 0x%x len 0x%x\n", io_addr, len);

  return IoBitmap::GetCurrent()->SetIoBitmap(io_addr, len, /*enable=*/false);
}

#else
// zx_status_t zx_ioports_request
zx_status_t sys_ioports_request(zx_handle_t hrsrc, uint16_t io_addr, uint32_t len) {
  // doesn't make sense on non-x86
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_ioports_release
zx_status_t sys_ioports_release(zx_handle_t hrsrc, uint16_t io_addr, uint32_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}
#endif

// zx_status_t zx_msi_allocate
zx_status_t sys_msi_allocate(zx_handle_t root, uint32_t count, user_out_handle* out) {
  zx_status_t status;
  if ((status = validate_resource(root, ZX_RSRC_KIND_ROOT)) != ZX_OK) {
    return status;
  }

  fbl::RefPtr<MsiAllocation> alloc;
  if ((status = MsiAllocation::Create(count, &alloc)) != ZX_OK) {
    return status;
  }

  zx_rights_t rights;
  KernelHandle<MsiAllocationDispatcher> alloc_handle;
  if ((status = MsiAllocationDispatcher::Create(ktl::move(alloc), &alloc_handle, &rights)) !=
      ZX_OK) {
    return status;
  }

  return out->make(ktl::move(alloc_handle), rights);
}

// zx_status_t zx_msi_create
zx_status_t sys_msi_create(zx_handle_t msi_alloc, uint32_t options, uint32_t msi_id,
                           zx_handle_t vmo, size_t vmo_offset, user_out_handle* out) {
  auto* up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<MsiAllocationDispatcher> msi_alloc_disp;

  zx_status_t status;
  if ((status = up->handle_table().GetDispatcher(msi_alloc, &msi_alloc_disp)) != ZX_OK) {
    return status;
  }

  fbl::RefPtr<VmObjectDispatcher> vmo_disp;
  if ((status = up->handle_table().GetDispatcherWithRights(vmo, ZX_RIGHT_MAP, &vmo_disp)) !=
      ZX_OK) {
    return status;
  }

  zx_rights_t rights;
  KernelHandle<InterruptDispatcher> msi_handle;
  if ((status = MsiDispatcher::Create(
           msi_alloc_disp->msi_allocation(), /* msi_id= */ msi_id, vmo_disp->vmo(),
           /* cap_offset= */ vmo_offset, /* options= */ options, &rights, &msi_handle)) != ZX_OK) {
    return status;
  }

  return out->make(ktl::move(msi_handle), rights);
}

// zx_status_t zx_pc_firmware_tables
zx_status_t sys_pc_firmware_tables(zx_handle_t hrsrc, user_out_ptr<zx_paddr_t> acpi_rsdp,
                                   user_out_ptr<zx_paddr_t> smbios) {
  // TODO(fxbug.dev/30918): finer grained validation
  zx_status_t status;
  if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
    return status;
  }
#if ARCH_X86
  if ((status = acpi_rsdp.copy_to_user(bootloader.acpi_rsdp)) != ZX_OK) {
    return status;
  }
  if ((status = smbios.copy_to_user(pc_get_smbios_entrypoint())) != ZX_OK) {
    return status;
  }

  return ZX_OK;
#endif
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_bti_create
zx_status_t sys_bti_create(zx_handle_t iommu, uint32_t options, uint64_t bti_id,
                           user_out_handle* out) {
  auto up = ProcessDispatcher::GetCurrent();

  if (options != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<IommuDispatcher> iommu_dispatcher;
  // TODO(teisenbe): This should probably have a right on it.
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(iommu, ZX_RIGHT_NONE, &iommu_dispatcher);
  if (status != ZX_OK) {
    return status;
  }

  KernelHandle<BusTransactionInitiatorDispatcher> handle;
  zx_rights_t rights;
  // TODO(teisenbe): Migrate BusTransactionInitiatorDispatcher::Create to
  // taking the iommu_dispatcher
  status = BusTransactionInitiatorDispatcher::Create(iommu_dispatcher->iommu(), bti_id, &handle,
                                                     &rights);
  if (status != ZX_OK) {
    return status;
  }

  return out->make(ktl::move(handle), rights);
}

// zx_status_t zx_bti_pin
zx_status_t sys_bti_pin(zx_handle_t handle, uint32_t options, zx_handle_t vmo, uint64_t offset,
                        uint64_t size, user_out_ptr<zx_paddr_t> addrs, size_t addrs_count,
                        user_out_handle* pmt) {
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<BusTransactionInitiatorDispatcher> bti_dispatcher;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_MAP, &bti_dispatcher);
  if (status != ZX_OK) {
    return status;
  }

  if (!IS_PAGE_ALIGNED(offset) || !IS_PAGE_ALIGNED(size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<VmObjectDispatcher> vmo_dispatcher;
  zx_rights_t vmo_rights;
  status = up->handle_table().GetDispatcherAndRights(vmo, &vmo_dispatcher, &vmo_rights);
  if (status != ZX_OK) {
    return status;
  }
  if (!(vmo_rights & ZX_RIGHT_MAP)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // Convert requested permissions and check against VMO rights
  uint32_t iommu_perms = 0;
  bool compress_results = false;
  bool contiguous = false;
  if (options & ZX_BTI_PERM_READ) {
    if (!(vmo_rights & ZX_RIGHT_READ)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    iommu_perms |= IOMMU_FLAG_PERM_READ;
    options &= ~ZX_BTI_PERM_READ;
  }
  if (options & ZX_BTI_PERM_WRITE) {
    if (!(vmo_rights & ZX_RIGHT_WRITE)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    iommu_perms |= IOMMU_FLAG_PERM_WRITE;
    options &= ~ZX_BTI_PERM_WRITE;
  }
  if (options & ZX_BTI_PERM_EXECUTE) {
    // Note: We check ZX_RIGHT_READ instead of ZX_RIGHT_EXECUTE
    // here because the latter applies to execute permission of
    // the host CPU, whereas ZX_BTI_PERM_EXECUTE applies to
    // transactions initiated by the bus device.
    if (!(vmo_rights & ZX_RIGHT_READ)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    iommu_perms |= IOMMU_FLAG_PERM_EXECUTE;
    options &= ~ZX_BTI_PERM_EXECUTE;
  }
  if (!((options & ZX_BTI_COMPRESS) && (options & ZX_BTI_CONTIGUOUS))) {
    if (options & ZX_BTI_COMPRESS) {
      compress_results = true;
      options &= ~ZX_BTI_COMPRESS;
    }
    if (options & ZX_BTI_CONTIGUOUS && vmo_dispatcher->vmo()->is_contiguous()) {
      contiguous = true;
      options &= ~ZX_BTI_CONTIGUOUS;
    }
  }
  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  constexpr size_t kAddrsLenLimitForStack = 4;
  fbl::AllocChecker ac;
  fbl::InlineArray<dev_vaddr_t, kAddrsLenLimitForStack> mapped_addrs(&ac, addrs_count);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  KernelHandle<PinnedMemoryTokenDispatcher> new_pmt_handle;
  zx_rights_t new_pmt_rights;
  status = bti_dispatcher->Pin(vmo_dispatcher->vmo(), offset, size, iommu_perms, &new_pmt_handle,
                               &new_pmt_rights);
  if (status != ZX_OK) {
    return status;
  }

  // If anything goes wrong from here on out, we _must_ remember to unpin the
  // PMT we are holding.  Failure to do this means that the PMT will hit
  // on-zero-handles while it still has pages pinned and end up in the BTI's
  // quarantine list.  This is definitely not correct as the user never got
  // access to the PMT handle in order to unpin the data.
  //
  // Notice that we're holding a RefPtr to the dispatcher rather than a
  // reference to the |new_pmt_handle|.  Just before we return, |new_pmt_handle|
  // will be moved in order to make a user_out_handle.  |new_pmt_handle| will
  // not be valid after the move so we keep a RefPtr to the dispatcher instead.
  auto cleanup = fbl::MakeAutoCall([disp = new_pmt_handle.dispatcher()]() { disp->Unpin(); });

  status = new_pmt_handle.dispatcher()->EncodeAddrs(compress_results, contiguous,
                                                    mapped_addrs.get(), addrs_count);
  if (status != ZX_OK) {
    return status;
  }

  static_assert(sizeof(dev_vaddr_t) == sizeof(zx_paddr_t), "mismatched types");
  if ((status = addrs.copy_array_to_user(mapped_addrs.get(), addrs_count)) != ZX_OK) {
    return status;
  }

  zx_status_t res = pmt->make(ktl::move(new_pmt_handle), new_pmt_rights);
  if (res == ZX_OK) {
    cleanup.cancel();
  }

  return res;
}

// zx_status_t zx_bti_release_quarantine
zx_status_t sys_bti_release_quarantine(zx_handle_t handle) {
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<BusTransactionInitiatorDispatcher> bti_dispatcher;

  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &bti_dispatcher);
  if (status != ZX_OK) {
    return status;
  }

  bti_dispatcher->ReleaseQuarantine();
  return ZX_OK;
}

// Having a single-purpose syscall like this is a bit of an anti-pattern in our
// syscall API, but we feel there is benefit in this over trying to extend the
// semantics of handle closing in sys_handle_close and process death.  In
// particular, PMTs are the only objects in the system that track the lifetime
// of something external to the process model (external hardware DMA
// capabilities).
// zx_status_t zx_pmt_unpin
zx_status_t sys_pmt_unpin(zx_handle_t handle) {
  auto up = ProcessDispatcher::GetCurrent();

  HandleOwner handle_owner = up->handle_table().RemoveHandle(handle);
  if (!handle_owner) {
    return ZX_ERR_BAD_HANDLE;
  }

  fbl::RefPtr<Dispatcher> dispatcher = handle_owner->dispatcher();
  auto pmt_dispatcher = DownCastDispatcher<PinnedMemoryTokenDispatcher>(&dispatcher);
  if (!pmt_dispatcher) {
    return ZX_ERR_WRONG_TYPE;
  }

  pmt_dispatcher->Unpin();

  return ZX_OK;
}

// zx_status_t zx_interrupt_create
zx_status_t sys_interrupt_create(zx_handle_t src_obj, uint32_t src_num, uint32_t options,
                                 user_out_handle* out_handle) {
  LTRACEF("options 0x%x\n", options);

  // resource not required for virtual interrupts
  if (!(options & ZX_INTERRUPT_VIRTUAL)) {
    zx_status_t status;
    if ((status = validate_resource_irq(src_obj, src_num)) != ZX_OK) {
      return status;
    }
  }

  KernelHandle<InterruptDispatcher> handle;
  zx_rights_t rights;
  zx_status_t result;
  if (options & ZX_INTERRUPT_VIRTUAL) {
    result = VirtualInterruptDispatcher::Create(&handle, &rights, options);
  } else {
    result = InterruptEventDispatcher::Create(&handle, &rights, src_num, options);
  }
  if (result != ZX_OK) {
    return result;
  }

  return out_handle->make(ktl::move(handle), rights);
}

// zx_status_t zx_interrupt_bind
zx_status_t sys_interrupt_bind(zx_handle_t handle, zx_handle_t port_handle, uint64_t key,
                               uint32_t options) {
  LTRACEF("handle %x\n", handle);
  if ((options != ZX_INTERRUPT_BIND) && (options != ZX_INTERRUPT_UNBIND)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<InterruptDispatcher> interrupt;
  status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_READ, &interrupt);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<PortDispatcher> port;
  status = up->handle_table().GetDispatcherWithRights(port_handle, ZX_RIGHT_WRITE, &port);
  if (status != ZX_OK) {
    return status;
  }
  if (!port->can_bind_to_interrupt()) {
    return ZX_ERR_WRONG_TYPE;
  }

  if (options == ZX_INTERRUPT_BIND) {
    return interrupt->Bind(ktl::move(port), key);
  } else {
    return interrupt->Unbind(ktl::move(port));
  }
}

// zx_status_t zx_interrupt_bind_vcpu
zx_status_t sys_interrupt_bind_vcpu(zx_handle_t handle, zx_handle_t vcpu, uint32_t options) {
  LTRACEF("handle %x\n", handle);

  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<InterruptDispatcher> interrupt_dispatcher;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_READ, &interrupt_dispatcher);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<VcpuDispatcher> vcpu_dispatcher;
  status = up->handle_table().GetDispatcherWithRights(vcpu, ZX_RIGHT_WRITE, &vcpu_dispatcher);
  if (status != ZX_OK) {
    return status;
  }

  return interrupt_dispatcher->BindVcpu(ktl::move(vcpu_dispatcher));
}

// zx_status_t zx_interrupt_ack
zx_status_t sys_interrupt_ack(zx_handle_t inth) {
  LTRACEF("handle %x\n", inth);

  zx_status_t status;
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<InterruptDispatcher> interrupt;
  status = up->handle_table().GetDispatcherWithRights(inth, ZX_RIGHT_WRITE, &interrupt);
  if (status != ZX_OK) {
    return status;
  }
  return interrupt->Ack();
}

// zx_status_t zx_interrupt_wait
zx_status_t sys_interrupt_wait(zx_handle_t handle, user_out_ptr<zx_time_t> out_timestamp) {
  LTRACEF("handle %x\n", handle);

  zx_status_t status;
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<InterruptDispatcher> interrupt;
  status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_WAIT, &interrupt);
  if (status != ZX_OK) {
    return status;
  }

  zx_time_t timestamp;
  status = interrupt->WaitForInterrupt(&timestamp);
  if (status == ZX_OK && out_timestamp) {
    status = out_timestamp.copy_to_user(timestamp);
  }

  return status;
}

// zx_status_t zx_interrupt_destroy
zx_status_t sys_interrupt_destroy(zx_handle_t handle) {
  LTRACEF("handle %x\n", handle);

  zx_status_t status;
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<InterruptDispatcher> interrupt;
  status = up->handle_table().GetDispatcher(handle, &interrupt);
  if (status != ZX_OK) {
    return status;
  }

  return interrupt->Destroy();
}

// zx_status_t zx_interrupt_trigger
zx_status_t sys_interrupt_trigger(zx_handle_t handle, uint32_t options, zx_time_t timestamp) {
  LTRACEF("handle %x\n", handle);

  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<InterruptDispatcher> interrupt;
  status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_SIGNAL, &interrupt);
  if (status != ZX_OK) {
    return status;
  }

  return interrupt->Trigger(timestamp);
}

// zx_status_t zx_smc_call
zx_status_t sys_smc_call(zx_handle_t handle, user_in_ptr<const zx_smc_parameters_t> parameters,
                         user_out_ptr<zx_smc_result_t> out_smc_result) {
  if (!parameters || !out_smc_result) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_smc_parameters_t params;
  zx_status_t status = parameters.copy_from_user(&params);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t service_call_num = ARM_SMC_GET_SERVICE_CALL_NUM_FROM_FUNC_ID(params.func_id);
  if ((status = validate_resource_smc(handle, service_call_num)) != ZX_OK) {
    return status;
  }

  zx_smc_result_t result;

  arch_smc_call(&params, &result);

  return out_smc_result.copy_to_user(result);
}
