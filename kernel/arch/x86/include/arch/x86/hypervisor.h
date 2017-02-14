// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <mxtl/array.h>
#include <mxtl/unique_ptr.h>

struct VmxInfo;
class VmxCpuContext;

class VmxContext {
public:
    static mx_status_t Create(mxtl::unique_ptr<VmxContext>* context);

    ~VmxContext();

    VmxCpuContext* CurrCpuContext();

private:
    mxtl::Array<VmxCpuContext> cpu_contexts_;

    explicit VmxContext(mxtl::Array<VmxCpuContext> cpu_contexts);

    mx_status_t AllocCpuContexts(const VmxInfo& info);
    mx_status_t CpuContextStatus();
};

using HypervisorContext = VmxContext;
