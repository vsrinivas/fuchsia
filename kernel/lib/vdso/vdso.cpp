// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/vdso.h>
#include <lib/vdso-constants.h>

#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <mxalloc/new.h>
#include <mxtl/type_support.h>
#include <platform.h>

#include "vdso-code.h"

// This is defined in assembly by vdso-image.S; vdso-code.h
// gives details about the image's size and layout.
extern "C" const char vdso_image[];

namespace {

// Each KernelVmoWindow object represents a mapping in the kernel address
// space of a T object found inside a VM object.  The kernel mapping exists
// for the lifetime of the KernelVmoWindow object.
template<typename T>
class KernelVmoWindow {
public:
    static_assert(mxtl::is_pod<T>::value,
                  "this is for C-compatible types only!");

    KernelVmoWindow(const char* name,
                    mxtl::RefPtr<VmObject> vmo, uint64_t offset)
        : mapping_(nullptr) {
        uint64_t page_offset = ROUNDDOWN(offset, PAGE_SIZE);
        size_t offset_in_page = static_cast<size_t>(offset % PAGE_SIZE);
        ASSERT(offset % alignof(T) == 0);

        const size_t size = offset_in_page + sizeof(T);
        const uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        status_t status = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
                0 /* ignored */, size, 0 /* align pow2 */, 0 /* vmar flags */,
                mxtl::move(vmo), page_offset, arch_mmu_flags, name, &mapping_);
        ASSERT(status == NO_ERROR);
        data_ = reinterpret_cast<T*>(mapping_->base() + offset_in_page);
    }

    ~KernelVmoWindow() {
        if (mapping_) {
            status_t status = mapping_->Destroy();
            ASSERT(status == NO_ERROR);
        }
    }

    T* data() const { return data_; }

private:
    mxtl::RefPtr<VmMapping> mapping_;
    T* data_;
};

// The .dynsym section of the vDSO, an array of ELF symbol table entries.
struct VDsoDynSym {
    struct {
        uintptr_t info, value, size;
    } table[VDSO_DYNSYM_COUNT];
};

#define PASTE(a, b, c) PASTE_1(a, b, c)
#define PASTE_1(a, b, c) a##b##c

class VDsoDynSymWindow {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VDsoDynSymWindow);

    static_assert(sizeof(VDsoDynSym) ==
                  VDSO_DATA_END_dynsym - VDSO_DATA_START_dynsym,
                  "either VDsoDynsym or gen-rodso-code.sh is suspect");

    explicit VDsoDynSymWindow(mxtl::RefPtr<VmObject> vmo) :
        window_("vDSO .dynsym", mxtl::move(vmo), VDSO_DATA_START_dynsym) {}

    void set_symbol_entry(size_t i, uintptr_t value, uintptr_t size) {
        window_.data()->table[i].value = value;
        window_.data()->table[i].size = size;
    }

#define set_symbol(symbol, target)                      \
    set_symbol_entry(PASTE(VDSO_DYNSYM_, symbol,),      \
                     PASTE(VDSO_CODE_, target,),        \
                     PASTE(VDSO_CODE_, target, _SIZE))

private:
    KernelVmoWindow<VDsoDynSym> window_;
};

#define REDIRECT_SYSCALL(dynsym_window, symbol, target)         \
    do {                                                        \
        dynsym_window.set_symbol(symbol, target);               \
        dynsym_window.set_symbol(_ ## symbol, target);          \
    } while (0)

}; // anonymous namespace

const VDso* VDso::instance_ = NULL;

// Private constructor, can only be called by Create (below).
VDso::VDso() : RoDso("vdso", vdso_image, VDSO_CODE_END, VDSO_CODE_START) {}

// This is called exactly once, at boot time.
const VDso* VDso::Create() {
    ASSERT(!instance_);

    AllocChecker ac;
    instance_ = new(&ac) VDso();
    ASSERT(ac.check());

    // Map a window into the VMO to write the vdso_constants struct.
    static_assert(sizeof(vdso_constants) == VDSO_DATA_CONSTANTS_SIZE,
                  "gen-rodso-code.sh is suspect");
    KernelVmoWindow<vdso_constants> constants_window(
        "vDSO constants", instance_->vmo()->vmo(), VDSO_DATA_CONSTANTS);
    uint64_t per_second = ticks_per_second();

    // Initialize the constants that should be visible to the vDSO.
    // Rather than assigning each member individually, do this with
    // struct assignment and a compound literal so that the compiler
    // can warn if the initializer list omits any member.
    *constants_window.data() = (vdso_constants) {
        arch_max_num_cpus(),
        arch_dcache_line_size(),
        per_second,
        pmm_count_total_bytes(),
    };

    // If ticks_per_second has not been calibrated, it will return 0. In this
    // case, use soft_ticks instead.
    if (per_second == 0 || cmdline_get_bool("vdso.soft_ticks", false)) {
        // Make mx_ticks_per_second return nanoseconds per second.
        constants_window.data()->ticks_per_second = MX_SEC(1);

        // Adjust the mx_ticks_get entry point to be soft_ticks_get.
        VDsoDynSymWindow dynsym_window(instance_->vmo()->vmo());
        REDIRECT_SYSCALL(dynsym_window, mx_ticks_get, soft_ticks_get);
    }

    return instance_;
}

uintptr_t VDso::base_address(const mxtl::RefPtr<VmMapping>& code_mapping) {
    return code_mapping ? code_mapping->base() - VDSO_CODE_START : 0;
}
