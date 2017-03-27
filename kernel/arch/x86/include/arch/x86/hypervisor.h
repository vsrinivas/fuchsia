// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <mxtl/array.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

typedef struct vm_page vm_page_t;
class VmObject;
struct VmxInfo;
class VmxonPerCpu;
class VmcsPerCpu;
class GuestPhysicalAddressSpace;

class VmxPage {
public:
    ~VmxPage();

    mx_status_t Alloc(const VmxInfo& info);
    paddr_t PhysicalAddress();
    void* VirtualAddress();

    template<typename T>
    T* VirtualAddress() {
        return static_cast<T*>(VirtualAddress());
    }

    bool IsAllocated() { return pa_ != 0; }

private:
    paddr_t pa_ = 0;
};

class VmxonContext {
public:
    static mx_status_t Create(mxtl::unique_ptr<VmxonContext>* context);

    ~VmxonContext();

    VmxonPerCpu* PerCpu();

private:
    mxtl::Array<VmxonPerCpu> per_cpus_;

    explicit VmxonContext(mxtl::Array<VmxonPerCpu> per_cpus);
};

class VmcsContext {
public:
    static mx_status_t Create(mxtl::RefPtr<VmObject> vmo,
                              mxtl::unique_ptr<VmcsContext>* context);

    ~VmcsContext();

    paddr_t Pml4Address();
    VmcsPerCpu* PerCpu();
    mx_status_t Start();

private:
    mxtl::unique_ptr<GuestPhysicalAddressSpace> gpas_;
    mxtl::Array<VmcsPerCpu> per_cpus_;

    explicit VmcsContext(mxtl::Array<VmcsPerCpu> per_cpus);
};

using HypervisorContext = VmxonContext;
using GuestContext = VmcsContext;
