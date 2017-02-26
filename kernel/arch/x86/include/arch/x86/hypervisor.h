// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <mxtl/array.h>
#include <mxtl/unique_ptr.h>

typedef struct vm_page vm_page_t;
struct VmxInfo;
class VmxonCpuContext;
class VmcsCpuContext;

class VmxPage {
public:
    ~VmxPage();

    mx_status_t Alloc(const VmxInfo& info);
    paddr_t PhysicalAddress();
    void* VirtualAddress();

private:
    paddr_t pa_;
    vm_page_t* page_ = nullptr;
};

class VmxonContext {
public:
    static mx_status_t Create(mxtl::unique_ptr<VmxonContext>* context);

    ~VmxonContext();

    VmxonCpuContext* CurrCpuContext();

private:
    mxtl::Array<VmxonCpuContext> cpu_contexts_;

    explicit VmxonContext(mxtl::Array<VmxonCpuContext> cpu_contexts);
};

class VmcsContext {
public:
    static mx_status_t Create(mxtl::unique_ptr<VmcsContext>* context);

    ~VmcsContext();

    VmcsCpuContext* CurrCpuContext();
    mx_status_t Start(uintptr_t entry, uintptr_t stack);

private:
    mxtl::Array<VmcsCpuContext> cpu_contexts_;

    explicit VmcsContext(mxtl::Array<VmcsCpuContext> cpu_contexts);
};

using HypervisorContext = VmxonContext;
using GuestContext = VmcsContext;
