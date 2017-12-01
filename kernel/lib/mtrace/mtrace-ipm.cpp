// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// N.B. This is ideally temporary.
// It is used by Intel HW Performonce Monitor support, and is a stopgap until
// "resources" can be used to read/write x86 MSRs.
// "mtrace" == "zircon trace": the idea being to be a generalization of
// ktrace. It's all temporary, but there may be other uses before the stopgap
// is no longer necessary.

#ifdef __x86_64__ // entire file

#include <inttypes.h>

#include "lib/mtrace.h"
#include "trace.h"

#include <arch/user_copy.h>
#include <object/process_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <zircon/mtrace.h>

#include "arch/x86/perf_mon.h"

#define LOCAL_TRACE 0

zx_status_t mtrace_ipm_control(uint32_t action, uint32_t options,
                               user_inout_ptr<void> arg, uint32_t size) {
    TRACEF("action %u, options 0x%x, arg %p, size 0x%x\n",
           action, options, arg.get(), size);

    switch (action) {
    case MTRACE_IPM_GET_PROPERTIES: {
        zx_x86_ipm_properties_t props;
        if (size != sizeof(props))
            return ZX_ERR_INVALID_ARGS;
        if (options != 0)
            return ZX_ERR_INVALID_ARGS;
        auto status = x86_ipm_get_properties(&props);
        if (status != ZX_OK)
            return status;
        status = arg.reinterpret<zx_x86_ipm_properties_t>().copy_to_user(props);
        if (status != ZX_OK)
            return status;
        return ZX_OK;
    }

    case MTRACE_IPM_INIT:
        if (options != 0 || size != 0)
            return ZX_ERR_INVALID_ARGS;
        return x86_ipm_init();

    case MTRACE_IPM_ASSIGN_BUFFER: {
        zx_x86_ipm_buffer_t buffer;
        if (size != sizeof(buffer))
            return ZX_ERR_INVALID_ARGS;
        zx_status_t status = arg.reinterpret<zx_x86_ipm_buffer_t>().copy_from_user(&buffer);
        if (status != ZX_OK)
            return status;

        // TODO(dje): Later need to rework to assign buffers to things
        // like threads.
        uint32_t cpu = MTRACE_IPM_OPTIONS_CPU(options);
        if ((options & ~MTRACE_IPM_OPTIONS_CPU_MASK) != 0)
            return ZX_ERR_INVALID_ARGS;

        // lookup the VMO dispatcher from handle
        // TODO(dje): Passing in a vmo from userspace, even from a device
        // driver we control, to which we will write from kernel space, feels
        // dodgey. Perhaps we should allocate the vmo here, but that put more
        // of this driver in kernel space. Revisit.
        auto up = ProcessDispatcher::GetCurrent();
        fbl::RefPtr<VmObjectDispatcher> vmo;
        zx_rights_t vmo_rights;
        zx_rights_t needed_rights =
            ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE;
        status = up->GetDispatcherWithRights(buffer.vmo, needed_rights,
                                             &vmo, &vmo_rights);
        if (status != ZX_OK)
            return status;

        return x86_ipm_assign_buffer(cpu, fbl::move(vmo->vmo()));
    }

    case MTRACE_IPM_STAGE_CONFIG: {
        zx_x86_ipm_config_t config;
        if (size != sizeof(config))
            return ZX_ERR_INVALID_ARGS;
        zx_status_t status = arg.reinterpret<zx_x86_ipm_config_t>().copy_from_user(&config);
        if (status != ZX_OK)
            return status;
        if (options != 0)
            return ZX_ERR_INVALID_ARGS;
        TRACEF("action %u, global_ctrl 0x%" PRIx64 "\n",
               action, config.global_ctrl);
        return x86_ipm_stage_config(&config);
    }

    case MTRACE_IPM_START:
        if (options != 0 || size != 0)
            return ZX_ERR_INVALID_ARGS;
        return x86_ipm_start();

    case MTRACE_IPM_STOP:
        if (options != 0 || size != 0)
            return ZX_ERR_INVALID_ARGS;
        return x86_ipm_stop();

    case MTRACE_IPM_FINI:
        if (options != 0 || size != 0)
            return ZX_ERR_INVALID_ARGS;
        return x86_ipm_fini();

    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

#endif
