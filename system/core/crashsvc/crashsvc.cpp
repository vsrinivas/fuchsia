// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
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
    fbl::unique_ptr<zx_koid_t[]> child_koids;
    size_t num_koids;
    if (GetChildKoids(job, ZX_INFO_JOB_PROCESSES, &child_koids, &num_koids)) {
        for (size_t i = 0; i < num_koids; ++i) {
            if (child_koids[i] == process_koid) {
                zx::process process;
                if (job.get_child(child_koids[i], ZX_RIGHT_SAME_RIGHTS, &process) != ZX_OK) {
                    return false;
                }
                *out = fbl::move(process);
                return true;
            }
        }
    }

    // Otherwise, search child jobs in the same way.
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

static void HandOffException(const zx::job& root_job, const zx::channel& channel,
                             const zx_port_packet_t& packet) {
    zx::process exception_process;
    if (!FindProcess(root_job, packet.exception.pid, &exception_process)) {
        fprintf(stderr, "crashsvc: failed to find process for pid=%zu\n", packet.exception.pid);
        return;
    }

    zx::thread exception_thread;
    if (exception_process.get_child(packet.exception.tid, ZX_RIGHT_SAME_RIGHTS,
                                    &exception_thread) != ZX_OK) {
        fprintf(stderr, "crashsvc: failed to find thread for tid=%zu\n", packet.exception.tid);
        return;
    }

    zx_handle_t handles[] = {exception_process.release(), exception_thread.release()};
    zx_status_t status =
        channel.write(0, &packet.type, sizeof(packet.type), handles, countof(handles));
    if (status != ZX_OK) {
        // If the channel write failed, things are going badly, attempt to
        // resume the excepted  thread which will typically result in the
        // process being terminated by the kernel.
        fprintf(stderr, "crashsvc: channel write failed: %d\n", status);
        status = zx_task_resume(handles[1], ZX_RESUME_EXCEPTION | ZX_RESUME_TRY_NEXT);
        if (status != ZX_OK) {
            fprintf(stderr, "crashsvc: zx_task_resume failed: %d\n", status);
        }
    }
}

// crashsvc watches the exception port on the root job and dispatches to
// an analyzer process that's responsible for handling the exception.
int main(int argc, char** argv) {
    fprintf(stderr, "crashsvc: starting\n");

    // crashsvc receives 3 handles at startup:
    // - the root job handle
    // - the exception port handle, already bound
    // - a channel on which to write messages when exceptions are encountered
    zx::job root_job(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    if (!root_job.is_valid()) {
        fprintf(stderr, "crashsvc: no root job\n");
        return 1;
    }
    zx::port exception_port(zx_take_startup_handle(PA_HND(PA_USER0, 1)));
    if (!exception_port.is_valid()) {
        fprintf(stderr, "crashsvc: no exception port\n");
        return 1;
    }
    zx::channel channel(zx_take_startup_handle(PA_HND(PA_USER0, 2)));
    if (!channel.is_valid()) {
        fprintf(stderr, "crashsvc: no channel\n");
        return 1;
    }

    for (;;) {
        zx_port_packet_t packet;
        zx_status_t status = exception_port.wait(zx::time::infinite(), &packet);
        if (status != ZX_OK) {
            fprintf(stderr, "crashsvc: zx_port_wait failed %d\n", status);
            continue;
        }

        HandOffException(root_job, channel, packet);
    }
}
