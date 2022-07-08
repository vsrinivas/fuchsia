// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/user_copy.h>
#include <vm/physmap.h>

#include "guest_copy_priv.h"

namespace {

// Returns the page address for a given page table entry.
//
// If the page address is for a large page, we additionally calculate the offset
// to the correct guest physical page that backs the large page.
zx_gpaddr_t PageAddress(zx_gpaddr_t pt_addr, zx_vaddr_t guest_vaddr, size_t level) {
  zx_gpaddr_t off = 0;
  if (IS_LARGE_PAGE(pt_addr)) {
    if (level == 1) {
      off = guest_vaddr & PAGE_OFFSET_MASK_HUGE;
    } else if (level == 2) {
      off = guest_vaddr & PAGE_OFFSET_MASK_LARGE;
    }
  }
  return (pt_addr & X86_PG_FRAME) + (off & X86_PG_FRAME);
}

// Finds the host physical address of the page containing the guest virtual
// address `guest_vaddr`, and calls the `apply` functor with the address.
template <typename F>
zx::status<> FindPage(GuestPageTable& gpt, zx_vaddr_t guest_vaddr, F apply) {
  // Attempt to find the translation within the TLB.
  auto [paddr, exists] = gpt.tlb.Find(guest_vaddr);
  if (exists) {
    apply(paddr);
    return zx::ok();
  }
  // Calculate indices into the page table.
  size_t indices[X86_PAGING_LEVELS] = {
      VADDR_TO_PML4_INDEX(guest_vaddr),
      VADDR_TO_PDP_INDEX(guest_vaddr),
      VADDR_TO_PD_INDEX(guest_vaddr),
      VADDR_TO_PT_INDEX(guest_vaddr),
  };
  // Traverse down each level of the page table from the root.
  zx_gpaddr_t pt_addr = gpt.cr3;
  size_t level = 0;
  for (; level < X86_PAGING_LEVELS; level++) {
    auto callback = [&pt_addr, indices, level](zx_paddr_t host_paddr) {
      pt_entry_t* pt = static_cast<pt_entry_t*>(paddr_to_physmap(host_paddr));
      pt_addr = pt[indices[level]];
    };
    zx_gpaddr_t guest_paddr = PageAddress(pt_addr, guest_vaddr, level);
    if (auto result = gpt.gpas.ForPage(guest_paddr, std::move(callback)); result.is_error()) {
      return result.take_error();
    } else if (IS_LARGE_PAGE(pt_addr)) {
      break;
    } else if (!IS_PAGE_PRESENT(pt_addr)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }
  // At the leaf level, update the TLB and call `apply`.
  zx_gpaddr_t guest_paddr = PageAddress(pt_addr, guest_vaddr, level);
  return gpt.gpas.ForPage(guest_paddr, [&gpt, guest_vaddr, &apply](zx_paddr_t host_paddr) {
    gpt.tlb.Insert(guest_vaddr, host_paddr);
    apply(host_paddr);
  });
}

// Copy from an address `guest` in the guest page tables, to and address `host`
// in the host kernel, using the `copy` functor.
template <typename F>
zx::status<> GuestCopy(GuestPageTable& gpt, void* guest, void* host, size_t len, F copy) {
  auto guest_vaddr = reinterpret_cast<zx_vaddr_t>(guest);
  auto apply = [&host, &len, &copy, &guest_vaddr](zx_paddr_t host_paddr) {
    // NOTE: While the guest may support large pages, Zircon currently does not.
    // So we must lookup each 4KB page.
    size_t page_offset = guest_vaddr & PAGE_OFFSET_MASK_4KB;
    void* host_vaddr = paddr_to_physmap(host_paddr + page_offset);
    size_t n = ktl::min(len, PAGE_SIZE - page_offset);
    copy(host_vaddr, host, n);
    guest_vaddr += n;
    host = reinterpret_cast<uint8_t*>(host) + n;
    len -= n;
  };
  // Find each page, and copy it one-by-one until `len` is 0.
  while (len != 0) {
    auto result = FindPage(gpt, guest_vaddr, apply);
    if (result.is_error()) {
      return result.take_error();
    }
  }
  return zx::ok();
}

}  // namespace

zx_status_t arch_copy_from_guest(GuestPageTable& gpt, void* dst, const void* src, size_t len) {
  auto copy = [](const void* src, void* dst, size_t n) {
    asm volatile("rep movsb"
                 : "=D"(dst), "=S"(src), "=c"(n)
                 : "0"(dst), "1"(src), "2"(n)
                 : "memory");
  };
  return GuestCopy(gpt, const_cast<void*>(src), dst, len, copy).status_value();
}

zx_status_t arch_copy_to_guest(GuestPageTable& gpt, void* dst, const void* src, size_t len) {
  auto copy = [](void* dst, const void* src, size_t n) {
    asm volatile("rep movsb"
                 : "=D"(dst), "=S"(src), "=c"(n)
                 : "0"(dst), "1"(src), "2"(n)
                 : "memory");
  };
  return GuestCopy(gpt, dst, const_cast<void*>(src), len, copy).status_value();
}
