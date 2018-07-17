// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/log-exporter.h>

#include <errno.h>
#include <stdint.h>

#include <fuchsia/logger/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

namespace runtests {
namespace {

fbl::String ToFblString(fidl_string_t string) {
    return fbl::String(string.data, string.size);
}

} // namespace

LogExporter::LogExporter(zx::channel channel, FILE* output_file)
    : loop_(&kAsyncLoopConfigNoAttachToThread),
      channel_(fbl::move(channel)),
      wait_(this, channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED),
      output_file_(output_file) {
    wait_.Begin(loop_.dispatcher());
}

LogExporter::~LogExporter() {
    // Quit so that current work is completed and loop can stop.
    loop_.Quit();

    // wait for current work to be completed.
    loop_.JoinThreads();

    // Run one more time until there are no more messages.
    loop_.ResetQuit();
    RunUntilIdle();

    // Shutdown
    loop_.Shutdown();
    if (output_file_ != nullptr) {
        fclose(output_file_);
    }
};

zx_status_t LogExporter::StartThread() {
    return loop_.StartThread();
}

zx_status_t LogExporter::RunUntilIdle() {
    return loop_.RunUntilIdle();
}

void LogExporter::OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                                const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        NotifyError(status);
        return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
        fidl::MessageBuffer buffer;
        for (uint64_t i = 0; i < signal->count; i++) {
            status = ReadAndDispatchMessage(&buffer);
            if (status == ZX_ERR_SHOULD_WAIT) {
                break;
            } else if (status != ZX_OK) {
                NotifyError(status);
                return;
            }
        }
        status = wait_.Begin(dispatcher);
        if (status != ZX_OK) {
            NotifyError(status);
        }
        return;
    }

    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);

    // We don't notify an error until we've drained all the messages.
    NotifyError(ZX_ERR_PEER_CLOSED);
}

zx_status_t LogExporter::ReadAndDispatchMessage(fidl::MessageBuffer* buffer) {
    fidl::Message message = buffer->CreateEmptyMessage();
    zx_status_t status = message.Read(channel_.get(), 0);
    if (status != ZX_OK) {
        return status;
    }
    if (!message.has_header()) {
        return ZX_ERR_INVALID_ARGS;
    }
    switch (message.ordinal()) {
    case fuchsia_logger_LogListenerLogOrdinal:
        return Log(fbl::move(message));
    case fuchsia_logger_LogListenerLogManyOrdinal:
        return LogMany(fbl::move(message));
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

// Returns only seconds part
uint64_t GetSeconds(uint64_t nanoseconds) {
    return nanoseconds / 1000000000UL;
}

// Returns only nano seconds part
uint64_t GetNanoSeconds(uint64_t nanoseconds) {
    return (nanoseconds / 1000UL) % 1000000UL;
}

#define RETURN_IF_ERROR(expr) \
    do {                      \
        int n = (expr);       \
        if (n < 0) {          \
            return n;         \
        }                     \
    } while (false)

int LogExporter::WriteSeverity(int32_t severity) {
    switch (severity) {
    case fuchsia_logger_LogLevelFilter_INFO:
        return fputs(" INFO", output_file_);
    case fuchsia_logger_LogLevelFilter_WARN:
        return fputs(" WARNING", output_file_);
    case fuchsia_logger_LogLevelFilter_ERROR:
        return fputs(" ERROR", output_file_);
    case fuchsia_logger_LogLevelFilter_FATAL:
        return fputs(" FATAL", output_file_);
    default:
        // all other cases severity would be a negative nuber so print it as
        // VLOG(n) where severity=-n
        return fprintf(output_file_, " VLOG(%d)", -severity);
    }
}

int LogExporter::LogMessage(fuchsia_logger_LogMessage* log_message) {
    RETURN_IF_ERROR(fprintf(output_file_, "[%05ld.%06ld][%lu][%lu]", GetSeconds(log_message->time),
                            GetNanoSeconds(log_message->time), log_message->pid, log_message->tid));
    RETURN_IF_ERROR(fputs("[", output_file_));
    fidl_string_t* tags = static_cast<fidl_string_t*>(log_message->tags.data);
    for (size_t i = 0; i < log_message->tags.count; ++i) {
        RETURN_IF_ERROR(fprintf(output_file_, "%s", ToFblString(tags[i]).c_str()));
        if (i < log_message->tags.count - 1) {
            RETURN_IF_ERROR(fputs(", ", output_file_));
        }
    }
    RETURN_IF_ERROR(fputs("]", output_file_));

    RETURN_IF_ERROR(WriteSeverity(log_message->severity));

    RETURN_IF_ERROR(fprintf(output_file_, ": %s\n", ToFblString(log_message->msg).c_str()));
    if (log_message->dropped_logs > 0) {
        bool log = true;
        bool found = false;
        for (DroppedLogs& dl : dropped_logs_) {
            if (dl.pid == log_message->pid) {
                found = true;
                // only update our vector when we get new dropped_logs value.
                if (dl.dropped_logs < log_message->dropped_logs) {
                    dl.dropped_logs = log_message->dropped_logs;
                } else {
                    log = false;
                }
                break;
            }
        }
        if (!found) {
            dropped_logs_.push_back(DroppedLogs{log_message->pid, log_message->dropped_logs});
        }
        if (log) {
            RETURN_IF_ERROR(fprintf(output_file_, "[%05ld.%06ld][%lu][%lu]", GetSeconds(log_message->time),
                                    GetNanoSeconds(log_message->time), log_message->pid,
                                    log_message->tid));
            RETURN_IF_ERROR(fputs("[", output_file_));
            fidl_string_t* tags = static_cast<fidl_string_t*>(log_message->tags.data);
            for (size_t i = 0; i < log_message->tags.count; ++i) {
                RETURN_IF_ERROR(fprintf(output_file_, "%s", ToFblString(tags[i]).c_str()));
                if (i < log_message->tags.count - 1) {
                    RETURN_IF_ERROR(fputs(", ", output_file_));
                }
            }
            RETURN_IF_ERROR(fputs("]", output_file_));
            RETURN_IF_ERROR(fprintf(output_file_, " WARNING: Dropped logs count:%d\n",
                                    log_message->dropped_logs));
        }
    }
    return 0;
}

zx_status_t LogExporter::Log(fidl::Message message) {
    const char* error_msg = nullptr;
    zx_status_t status = message.Decode(&fuchsia_logger_LogListenerLogRequestTable, &error_msg);
    if (status != ZX_OK) {
        fprintf(stderr, "log-listener: error: Log: %s\n", error_msg);
        return status;
    }

    fuchsia_logger_LogMessage* log_message = message.GetPayloadAs<fuchsia_logger_LogMessage>();
    if (LogMessage(log_message) < 0) {
        NotifyFileError(strerror(errno));
    }
    return ZX_OK;
}

zx_status_t LogExporter::LogMany(fidl::Message message) {
    const char* error_msg = nullptr;
    zx_status_t status = message.Decode(&fuchsia_logger_LogListenerLogManyRequestTable, &error_msg);
    if (status != ZX_OK) {
        fprintf(stderr, "log-listener: error: LogMany: %s\n", error_msg);
        return status;
    }

    fidl_vector_t* log_messages = message.GetPayloadAs<fidl_vector_t>();
    fuchsia_logger_LogMessage* msgs = static_cast<fuchsia_logger_LogMessage*>(log_messages->data);
    for (size_t i = 0; i < log_messages->count; ++i) {
        if (LogMessage(&msgs[i]) < 0) {
            NotifyFileError(strerror(errno));
            return ZX_OK;
        }
    }
    return ZX_OK;
}

void LogExporter::NotifyError(zx_status_t error) {
    channel_.reset();
    fclose(output_file_);
    output_file_ = nullptr;
    if (error_handler_) {
        error_handler_(error);
    }
}

void LogExporter::NotifyFileError(const char* error) {
    channel_.reset();
    fclose(output_file_);
    output_file_ = nullptr;
    if (file_error_handler_) {
        file_error_handler_(error);
    }
}

fbl::unique_ptr<LogExporter> LaunchLogExporter(const fbl::StringPiece syslog_path,
                                               ExporterLaunchError* error) {
    *error = NO_ERROR;
    fbl::String syslog_path_str = fbl::String(syslog_path.data());
    FILE* syslog_file = fopen(syslog_path_str.c_str(), "w");
    if (syslog_file == nullptr) {
        printf("Error: Could not open syslog file: %s.\n", syslog_path_str.c_str());
        *error = OPEN_FILE;
        return nullptr;
    }

    // Try and connect to logger service if available. It would be only
    // available in garnet and above layer
    zx::channel logger, logger_request;
    zx_status_t status;

    status = zx::channel::create(0, &logger, &logger_request);
    if (status != ZX_OK) {
        printf("LaunchLogExporter: cannot create channel for logger service: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = CREATE_CHANNEL;
        return nullptr;
    }

    status = fdio_service_connect("/svc/fuchsia.logger.Log", logger_request.release());
    if (status != ZX_OK) {
        printf("LaunchLogExporter: cannot connect to logger service: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = CONNECT_TO_LOGGER_SERVICE;
        return nullptr;
    }

    // Create a log exporter channel and pass it to logger service.
    zx::channel listener, listener_request;
    status = zx::channel::create(0, &listener, &listener_request);
    if (status != ZX_OK) {
        printf("LaunchLogExporter: cannot create channel for listener: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = CREATE_CHANNEL;
        return nullptr;
    }
    fuchsia_logger_LogListenRequest req = {};
    req.hdr.ordinal = fuchsia_logger_LogListenOrdinal;
    req.log_listener = FIDL_HANDLE_PRESENT;
    zx_handle_t listener_handle = listener.release();
    status = logger.write(0, &req, sizeof(req), &listener_handle, 1);
    if (status != ZX_OK) {
        printf("LaunchLogExporter: cannot pass listener to logger service: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = FIDL_ERROR;
        return nullptr;
    }

    // Connect log exporter channel to object and start message loop on it.
    auto log_exporter = fbl::make_unique<LogExporter>(fbl::move(listener_request),
                                                      syslog_file);
    log_exporter->set_error_handler([](zx_status_t status) {
        if (status != ZX_ERR_CANCELED) {
            printf("log exporter: Failed: %d (%s).\n",
                   status, zx_status_get_string(status));
        }
    });
    log_exporter->set_file_error_handler([](const char* error) {
        printf("log exporter: Error writing to file: %s.\n", error);
    });
    status = log_exporter->StartThread();
    if (status != ZX_OK) {
        printf("LaunchLogExporter: Failed to start log exporter: %d (%s).\n",
               status, zx_status_get_string(status));
        *error = START_LISTENER;
        return nullptr;
    }
    return log_exporter;
}

} // namespace runtests
