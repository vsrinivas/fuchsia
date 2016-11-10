// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/vdso.h>
#include <lib/vdso-constants.h>

#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

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
    KernelVmoWindow(const char* name,
                    mxtl::RefPtr<VmObject> vmo, uint64_t offset)
        : mapping_(0) {
        uint64_t page_offset = ROUNDDOWN(offset, PAGE_SIZE);
        size_t offset_in_page = static_cast<size_t>(offset % PAGE_SIZE);
        ASSERT(offset % alignof(T) == 0);
        void* ptr;
        status_t status = VmAspace::kernel_aspace()->MapObject(
            mxtl::move(vmo), name, page_offset, offset_in_page + sizeof(T),
            &ptr, 0, 0, 0, ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
        ASSERT(status == NO_ERROR);
        mapping_ = reinterpret_cast<uintptr_t>(ptr);
        data_ = reinterpret_cast<T*>(mapping_ + offset_in_page);
    }

    ~KernelVmoWindow() {
        if (mapping_ != 0) {
            status_t status = VmAspace::kernel_aspace()->FreeRegion(mapping_);
            ASSERT(status == NO_ERROR);
        }
    }

    T* data() const { return data_; }

private:
    uintptr_t mapping_;
    T* data_;
};

}; // anonymous namespace

VDso::VDso() : RoDso("vdso", vdso_image, VDSO_CODE_END, VDSO_CODE_START) {
    // Map a window into the VMO to write the vdso_constants struct.
    KernelVmoWindow<vdso_constants> constants_window(
        "vDSO constants", vmo()->vmo(), VDSO_DATA_CONSTANTS);

    // Initialize the constants that should be visible to the vDSO.
    // Rather than assigning each member individually, do this with
    // struct assignment and a compound literal so that the compiler
    // can warn if the initializer list omits any member.
    *constants_window.data() = (vdso_constants) {
        arch_max_num_cpus(),
    };
}
