// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <crashsvc/crashsvc.h>

#include <stdint.h>
#include <stdio.h>
#include <threads.h>

#include <fbl/unique_ptr.h>
#include <fuchsia/crash/c/fidl.h>
#include <inspector/inspector.h>

#include <lib/backtrace-request/backtrace-request-utils.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>

namespace {

enum PacketKey {
    kExceptionPacketKey,
    kJobTerminatedPacketKey
};

} // namespace

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

// Cleans up and resumes a thread in a manual backtrace request.
//
// This may modify |regs| via cleanup_backtrace_request().
//
// Returns true if this was a backtrace request and it successfully resumed.
static bool ResumeIfBacktraceRequest(const zx::thread& thread, const zx::port& exception_port,
                                     const zx_port_packet_t& packet,
                                     zx_excp_type_t exception_type,
                                     zx_thread_state_general_regs_t* regs) {
    if (is_backtrace_request(exception_type, regs)) {
        zx_status_t status = cleanup_backtrace_request(thread.get(), regs);
        if (status != ZX_OK) {
            fprintf(stderr, "crashsvc: failed to cleanup backtrace on thread %zu.%zu: %s (%d)\n",
                    packet.exception.pid, packet.exception.tid, zx_status_get_string(status),
                    status);
            return false;
        }

        // Mark the exception as handled so the thread resumes execution.
        status = thread.resume_from_exception(exception_port, 0);
        if (status != ZX_OK) {
            fprintf(stderr, "crashsvc: failed to resume thread %zu.%zu from backtrace: %s (%d)\n",
                    packet.exception.pid, packet.exception.tid, zx_status_get_string(status),
                    status);
            return false;
        }

        return true;
    }

    return false;
}

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

    // Dump the crash info to the logs whether we have a FIDL analyzer or not.
    zx_excp_type_t type;
    zx_thread_state_general_regs_t regs;
    inspector_print_debug_info(process.get(), thread.get(), &type, &regs);

    // A backtrace request should just dump and continue.
    if (ResumeIfBacktraceRequest(thread, ctx.exception_port, packet, type, &regs)) {
        return;
    }

    if (ctx.svc_request.is_valid()) {
        // Use the full system analyzer FIDL service, presumably crashpad_analyzer.
        zx::thread thread_copy;
        thread.duplicate(ZX_RIGHT_SAME_RIGHTS, &thread_copy);

        fuchsia_crash_Analyzer_OnNativeException_Result analyzer_result;
        const zx_status_t exception_status = fuchsia_crash_AnalyzerOnNativeException(
            ctx.svc_request.get(), process.release(), thread_copy.release(), &analyzer_result);

        if ((exception_status != ZX_OK) ||
            (analyzer_result.tag == fuchsia_crash_Analyzer_OnNativeException_ResultTag_err)) {
            fprintf(stderr, "crashsvc: analyzer failed, err (%d | %d)\n", exception_status,
                    analyzer_result.err);
        }
    }

    // Use RESUME_TRY_NEXT to pass it to the next handler, if we're the root
    // job handler this will kill the process.
    thread.resume_from_exception(ctx.exception_port, ZX_RESUME_TRY_NEXT);
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

        if (packet.key == kJobTerminatedPacketKey) {
            // We'll only get here in tests, if our job is actually the root job
            // the system will halt before sending the packet.
            return 0;
        }

        HandOffException(*ctx, packet);
    }
}

zx_status_t start_crashsvc(zx::job root_job, zx_handle_t analyzer_svc, thrd_t* thread) {

    zx::port exception_port;
    zx_status_t status = zx::port::create(0, &exception_port);
    if (status != ZX_OK) {
        fprintf(stderr, "svchost: unable to create port (error %d)\n", status);
        return status;
    }

    status = root_job.bind_exception_port(exception_port, kExceptionPacketKey, 0);
    if (status != ZX_OK) {
        fprintf(stderr, "svchost: unable to bind to job exception port (error %d)\n", status);
        return status;
    }

    // This isn't necessary for normal operation since the root job would take
    // down the entire system if it died, but it's low-cost and we want to be
    // able to stop the thread in tests.
    status = root_job.wait_async(exception_port, kJobTerminatedPacketKey, ZX_JOB_TERMINATED, 0);
    if (status != ZX_OK) {
        fprintf(stderr, "svchost: unable to wait for job signals (error %d)\n", status);
    }

    zx::channel ch0, ch1;

    if (analyzer_svc != ZX_HANDLE_INVALID) {
        zx::channel::create(0u, &ch0, &ch1);
        status = fdio_service_connect_at(
            analyzer_svc, fuchsia_crash_Analyzer_Name, ch0.release());
        if (status != ZX_OK) {
            fprintf(stderr, "svchost: unable to connect to analyzer service\n");
            return status;
        }
    }

    auto ctx = new crash_ctx{
        std::move(root_job),
        std::move(exception_port),
        std::move(ch1),
    };

    status = thrd_status_to_zx_status(thrd_create_with_name(thread, crash_svc, ctx, "crash-svc"));
    if (status != ZX_OK) {
        delete ctx;
    }
    return status;
}
