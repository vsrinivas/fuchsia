// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Code for listening to logger service and dumping the logs.
// This implements LogListener interface for logger fidl @ //zircon/system/fidl/fuchsia-logger.

#ifndef ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_LOG_EXPORTER_H_
#define ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_LOG_EXPORTER_H_

#include <fbl/string.h>
#include <fbl/vector.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/zx/channel.h>
#include <stdint.h>

// TODO(FIDL-182): Remove this once fixed.
typedef zx_handle_t fuchsia_logger_LogListener;
#include <fuchsia/logger/c/fidl.h>

namespace runtests {

// Error while launching LogExporter.
enum ExporterLaunchError {
    OPEN_FILE,
    CREATE_CHANNEL,
    FIDL_ERROR,
    CONNECT_TO_LOGGER_SERVICE,
    START_LISTENER,
    NO_ERROR,
};

// Listens to channel messages, converts them fidl log object and writes them to
// passed file object.
// This implements LogListener fidl interface.
// Example:
//    FILE* f = fopen("file", "w");
//    zx::channel channel;
//    //init above channel to link to logger service
//    ...
//    LogExporter l(fbl::move(channel), f);
//    l->StartThread();
class LogExporter {
public:
    using ErrorHandler = fbl::Function<void(zx_status_t)>;
    using FileErrorHandler = fbl::Function<void(const char* error)>;

    // Creates object and starts listening for msgs on channel written by Log
    // interface in logger fidl.
    //
    // |channel| channel to read log messages from.
    // |output_file| file to write logs to.
    //
    LogExporter(zx::channel channel, FILE* output_file);
    ~LogExporter();

    // Starts LogListener service on a seperate thread.
    //
    // Returns result of loop_.StartThread().
    zx_status_t StartThread();

    // Runs LogListener service until message loop is idle.
    //
    // Returns result of loop_.RunUntilIdle().
    zx_status_t RunUntilIdle();

    // Sets Error handler which would be called when there is an error
    // while serving |channel_|. If an error occurs, the channel will close and
    // the listener thread will stop.
    void set_error_handler(ErrorHandler error_handler) {
        error_handler_ = fbl::move(error_handler);
    }

    // Sets Error handler which would be called whenever there is an error
    // writing to file. If an error occurs, the channel will close and the
    // listener thread will stop.
    void set_file_error_handler(FileErrorHandler error_handler) {
        file_error_handler_ = fbl::move(error_handler);
    }

private:
    // Keeps track of the count of dropped logs for a process.
    struct DroppedLogs {
        uint64_t pid;
        uint32_t dropped_logs;
    };

    void OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal);

    // Decodes channel message and dispatches to correct handler.
    zx_status_t ReadAndDispatchMessage(fidl::MessageBuffer* buffer);

    // Implementation of LogListener Log method.
    zx_status_t Log(fidl::Message message);

    // Implementation of LogListener LogMany method.
    zx_status_t LogMany(fidl::Message message);

    // Helper method to log |message| to file.
    int LogMessage(fuchsia_logger_LogMessage* message);

    // Helper method to call |error_handler_|.
    void NotifyError(zx_status_t error);

    // Helper method to call |error_handler_|.
    void NotifyFileError(const char* error);

    // Helper method to write severity string.
    int WriteSeverity(int32_t severity);

    async::Loop loop_;
    zx::channel channel_;
    async::WaitMethod<LogExporter, &LogExporter::OnHandleReady> wait_;
    ErrorHandler error_handler_;
    FileErrorHandler file_error_handler_;

    FILE* output_file_;

    // Vector to keep track of dropped logs per pid
    fbl::Vector<DroppedLogs> dropped_logs_;
};

// Launches Log Exporter.
//
// Starts message loop on a seperate thread.
//
// |syslog_path| file path where to write logs.
// |error| error to set in case of failure.
//
// Returns nullptr if it is not possible to launch Log Exporter.
fbl::unique_ptr<LogExporter> LaunchLogExporter(fbl::StringPiece syslog_path,
                                               ExporterLaunchError* error);

} // namespace runtests

#endif // ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_LOG_EXPORTER_H_
