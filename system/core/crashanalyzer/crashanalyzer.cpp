// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include <fuchsia/crash/c/fidl.h>
#include <inspector/inspector.h>
#include <lib/async/cpp/wait.h>
#include <lib/crashanalyzer/crashanalyzer.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

static int verbosity_level = 0;

static zx_status_t handle_message(zx_handle_t channel, fidl::MessageBuffer* buffer) {
    fidl::Message message = buffer->CreateEmptyMessage();
    zx_status_t status = message.Read(channel, 0);
    if (status != ZX_OK)
        return status;
    if (!message.has_header())
        return ZX_ERR_INVALID_ARGS;
    switch (message.ordinal()) {
    case fuchsia_crash_AnalyzerHandleNativeExceptionOrdinal: {
        const char* error_msg = nullptr;
        zx_status_t status = message.Decode(&fuchsia_crash_AnalyzerHandleNativeExceptionRequestTable, &error_msg);
        if (status != ZX_OK) {
            fprintf(stderr, "crashanalyzer: error: %s\n", error_msg);
            return status;
        }
        auto* request = message.GetBytesAs<fuchsia_crash_AnalyzerHandleNativeExceptionRequest>();

        fuchsia_crash_AnalyzerHandleNativeExceptionResponse response;
        memset(&response, 0, sizeof(response));
        response.hdr.txid = request->hdr.txid;
        response.hdr.ordinal = request->hdr.ordinal;
        // TODO(DX-653): we should set a more meaningful status depending on
        // the result of process_report.
        response.status = ZX_OK;
        status = zx_channel_write(channel, 0, &response, sizeof(response), nullptr, 0);

        inspector_print_debug_info_and_resume_thread(request->process, request->thread, request->exception_port);
        zx_handle_close(request->thread);
        zx_handle_close(request->process);
        zx_handle_close(request->exception_port);

        return status;
    }
    case fuchsia_crash_AnalyzerHandleManagedRuntimeExceptionOrdinal: {
        fprintf(stderr, "crashanalyzer: error: No handling of managed runtime exception supported\n");

        const char* error_msg = nullptr;
        zx_status_t status = message.Decode(&fuchsia_crash_AnalyzerHandleManagedRuntimeExceptionRequestTable, &error_msg);
        if (status != ZX_OK) {
            fprintf(stderr, "crashanalyzer: error: %s\n", error_msg);
            return status;
        }
        auto* request = message.GetBytesAs<fuchsia_crash_AnalyzerHandleManagedRuntimeExceptionRequest>();
        zx_handle_close(request->stackTrace.vmo);

        return ZX_ERR_NOT_SUPPORTED;
    }
    case fuchsia_crash_AnalyzerProcessKernelPanicCrashlogOrdinal: {
        fprintf(stderr, "crashanalyzer: error: No processing of kernel panic crashlog supported\n");

        const char* error_msg = nullptr;
        zx_status_t status = message.Decode(&fuchsia_crash_AnalyzerProcessKernelPanicCrashlogRequestTable, &error_msg);
        if (status != ZX_OK) {
            fprintf(stderr, "crashanalyzer: error: %s\n", error_msg);
            return status;
        }
        auto* request = message.GetBytesAs<fuchsia_crash_AnalyzerProcessKernelPanicCrashlogRequest>();
        zx_handle_close(request->crashlog.vmo);

        return ZX_ERR_NOT_SUPPORTED;
    }
    default:
        fprintf(stderr, "crashanalyzer: error: Unknown message ordinal: %d\n", message.ordinal());
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void handle_ready(async_dispatcher_t* dispatcher,
                         async::Wait* wait,
                         zx_status_t status,
                         const zx_packet_signal_t* signal) {
    if (status != ZX_OK)
        goto done;

    if (signal->observed & ZX_CHANNEL_READABLE) {
        fidl::MessageBuffer buffer;
        for (uint64_t i = 0; i < signal->count; i++) {
            status = handle_message(wait->object(), &buffer);
            if (status == ZX_ERR_SHOULD_WAIT)
                break;
            if (status != ZX_OK)
                goto done;
        }
        status = wait->Begin(dispatcher);
        if (status != ZX_OK)
            goto done;
        return;
    }

    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
done:
    zx_handle_close(wait->object());
    delete wait;
}

static zx_status_t init(void** out_ctx) {
    inspector_set_verbosity(verbosity_level);

    // At debugging level 1 print our dso list (in case we crash in a way
    // that prevents printing it later).
    if (verbosity_level >= 1) {
        zx_handle_t self = zx_process_self();
        inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(self);
        printf("Crashlogger dso list:\n");
        inspector_dso_print_list(stdout, dso_list);
        inspector_dso_free_list(dso_list);
    }

    *out_ctx = nullptr;
    return ZX_OK;
}

static zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                           zx_handle_t request) {
    if (!strcmp(service_name, fuchsia_crash_Analyzer_Name)) {
        auto wait = new async::Wait(request,
                                    ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                    handle_ready);
        zx_status_t status = wait->Begin(dispatcher);

        if (status != ZX_OK) {
            delete wait;
            zx_handle_close(request);
            return status;
        }

        return ZX_OK;
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

static constexpr const char* crashanalyzer_services[] = {
    fuchsia_crash_Analyzer_Name,
    nullptr,
};

static constexpr zx_service_ops_t crashanalyzer_ops = {
    .init = init,
    .connect = connect,
    .release = nullptr,
};

static constexpr zx_service_provider_t crashanalyzer_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = crashanalyzer_services,
    .ops = &crashanalyzer_ops,
};

const zx_service_provider_t* crashanalyzer_get_service_provider() {
    return &crashanalyzer_service_provider;
}
