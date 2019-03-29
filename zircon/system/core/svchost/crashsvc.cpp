// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <threads.h>

#include <fbl/unique_ptr.h>
#include <fuchsia/crash/c/fidl.h>
#include <inspector/inspector.h>

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

static bool GetChildKoids(const zx::job& job, zx_object_info_topic_t child_kind,
                          fbl::unique_ptr<zx_koid_t[]>* koids, size_t* num_koids) {
    size_t actual = 0;
    size_t available = 0;

    size_t count = 100;
    koids->reset(new zx_koid_t[count]);

    for (;;) {
        if (job.get_info(child_kind, koids->get(), count * sizeof(zx_koid_t), &actual,
                         &available) != ZX_OK) {
            fprintf(stderr, "crashsvc: failed to get child koids\n");
            koids->reset();
            *num_koids = 0;
            return false;
        }

        if (actual == available) {
            break;
        }

        // Resize to the expected number next time with a bit of slop to try to
        // handle the race between here and the next request.
        count = available + 10;
        koids->reset(new zx_koid_t[count]);
    }

    // No need to actually downsize the output array since the size is separate.
    *num_koids = actual;
    return true;
}

static bool FindProcess(const zx::job& job, zx_koid_t process_koid, zx::process* out) {
    // Search this job for the process.
    zx::process process;
    if (job.get_child(process_koid, ZX_RIGHT_SAME_RIGHTS, &process) == ZX_OK) {
        *out = std::move(process);
        return true;
    }

    // Otherwise, enumerate and recurse into child jobs.
    fbl::unique_ptr<zx_koid_t[]> child_koids;
    size_t num_koids;
    if (GetChildKoids(job, ZX_INFO_JOB_CHILDREN, &child_koids, &num_koids)) {
        for (size_t i = 0; i < num_koids; ++i) {
            zx::job child_job;
            if (job.get_child(child_koids[i], ZX_RIGHT_SAME_RIGHTS, &child_job) != ZX_OK) {
                continue;
            }
            if (FindProcess(child_job, process_koid, out)) {
                return true;
            }
        }
    }

    return false;
}

struct crash_ctx {
    zx::job root_job;
    zx::port exception_port;
    zx::channel svc_request;
};

static void HandOffException(const crash_ctx& ctx, const zx_port_packet_t& packet) {
    zx::process process;
    if (!FindProcess(ctx.root_job, packet.exception.pid, &process)) {
        fprintf(stderr, "crashsvc: failed to find process for pid=%zu\n", packet.exception.pid);
        return;
    }

    zx::thread thread;
    const zx_status_t thread_status =
        process.get_child(packet.exception.tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (thread_status != ZX_OK) {
        fprintf(stderr, "crashsvc: failed to find thread for tid=%zu: %s (%d)\n",
                packet.exception.tid, zx_status_get_string(thread_status), thread_status);
        return;
    }

    if (ctx.svc_request.is_valid()) {
        // Use the full system analyzer FIDL service, presumably crashpad_analyzer.

        // First, we still dump the crash info in the logs.
        inspector_print_debug_info(process.get(), thread.get());

        zx::port port;
        const zx_status_t port_status = ctx.exception_port.duplicate(ZX_RIGHT_SAME_RIGHTS, &port);
        if (port_status != ZX_OK) {
            fprintf(stderr, "crashsvc: failed to duplicate exception port: %s (%d)\n",
                    zx_status_get_string(port_status), port_status);
            return;
        }
        // The resume_thread is only needed if the FIDL call fails.
        zx::thread resume_thread;
        thread.duplicate(ZX_RIGHT_SAME_RIGHTS, &resume_thread);

        fuchsia_crash_Analyzer_OnNativeException_Result analyzer_result;
        const zx_status_t exception_status = fuchsia_crash_AnalyzerOnNativeException(
            ctx.svc_request.get(), process.release(), thread.release(), port.release(),
            &analyzer_result);

        if ((exception_status != ZX_OK) ||
            (analyzer_result.tag == fuchsia_crash_Analyzer_OnNativeException_ResultTag_err)) {
            fprintf(stderr, "crashsvc: analyzer failed, err (%d | %d)\n", exception_status,
                    analyzer_result.err);
            if (resume_thread) {
                zx_task_resume_from_exception(resume_thread.get(), ctx.exception_port.get(),
                                              ZX_RESUME_TRY_NEXT);
            }
        }
    } else {
        // Use the zircon built-in analyzer. Does not return status so we presume
        // that upon failure it resumes the thread.
        inspector_print_debug_info_and_resume_thread(process.get(), thread.get(),
                                                     ctx.exception_port.get());
    }
}

int crash_svc(void* arg) {
    auto ctx = fbl::unique_ptr<crash_ctx>(reinterpret_cast<crash_ctx*>(arg));

    for (;;) {
        zx_port_packet_t packet;
        zx_status_t status = ctx->exception_port.wait(zx::time::infinite(), &packet);
        if (status != ZX_OK) {
            fprintf(stderr, "crashsvc: zx_port_wait failed %d\n", status);
            continue;
        }

        HandOffException(*ctx, packet);
    }
}

// Initialize the crash service, this supersedes the standalone service with the same
// name that lived in zircon/system/core/crashsvc/crashsvc.cpp (/boot/bin/crashsvc) and
// ad-hoc microservice in devmgr that delegated to svchost. See ZX-3199 for details.
//
// The job of this service is to handle exceptions that reached |root_job| and delegate
// the crash analysis to one of two services:
//
// - built-in : using system/ulib/inspector
// - appmgr hosted: via FIDL interface call (fuchsia_crash_Analyzer).
//
//  Which one depends if |analyzer_svc| is a valid channel handle, which svchost sets
//  depending on "use_system".
//
void start_crashsvc(zx::job root_job, zx_handle_t analyzer_svc) {

    zx::port exception_port;
    zx::port::create(0, &exception_port);

    if (root_job.bind_exception_port(exception_port, 0, 0) != ZX_OK) {
        fprintf(stderr, "svchost: unable to bind to root job exception port\n");
        return;
    }

    zx::channel ch0, ch1;

    if (analyzer_svc != ZX_HANDLE_INVALID) {
        zx::channel::create(0u, &ch0, &ch1);
        auto status = fdio_service_connect_at(
            analyzer_svc, fuchsia_crash_Analyzer_Name, ch0.release());
        if (status != ZX_OK) {
            fprintf(stderr, "svchost: unable to connect to analyzer service\n");
            return;
        }
    }

    auto ctx = new crash_ctx{
        std::move(root_job),
        std::move(exception_port),
        std::move(ch1),
    };

    thrd_t t;
    if ((thrd_create_with_name(&t, crash_svc, ctx, "crash-svc")) == thrd_success) {
        thrd_detach(t);
    } else {
        delete ctx;
    }
}
