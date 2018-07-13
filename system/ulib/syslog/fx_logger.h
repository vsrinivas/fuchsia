// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_SYSLOG_FX_LOGGER_H_
#define ZIRCON_SYSTEM_ULIB_SYSLOG_FX_LOGGER_H_

#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>

#include "syslog/logger.h"

namespace {

zx_koid_t GetKoid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_koid_t GetCurrentProcessKoid() {
    auto koid = GetKoid(zx_process_self());
    ZX_DEBUG_ASSERT(koid != ZX_KOID_INVALID);
    return koid;
}

} // namespace

struct fx_logger {
public:
    // If tags or ntags are out of bound, this constructor will not fail but it
    // will not store all the tags and global tag behaviour would be undefined.
    // So they should be validated before calling this constructor.
    fx_logger(const fx_logger_config_t* config) {
        pid_ = GetCurrentProcessKoid();
        socket_.reset(config->log_service_channel);
        fd_to_close_.reset(config->console_fd);
        logger_fd_.store(config->console_fd, fbl::memory_order_relaxed);
        SetSeverity(config->min_severity);
        ZX_DEBUG_ASSERT(fd_to_close_ != socket_.is_valid());
        AddTags(config->tags, config->num_tags);
        dropped_logs_.store(0, fbl::memory_order_relaxed);
    }

    ~fx_logger() = default;

    zx_status_t VLogWrite(fx_log_severity_t severity, const char* tag,
                          const char* format, va_list args) {
        return VLogWrite(severity, tag, format, args, true);
    }

    zx_status_t LogWrite(fx_log_severity_t severity, const char* tag,
                         const char* msg) {
        va_list empty_args;
        return VLogWrite(severity, tag, msg, empty_args, false);
    }

    void SetSeverity(fx_log_severity_t log_severity) {
        severity_.store(log_severity, fbl::memory_order_relaxed);
    }

    fx_log_severity_t GetSeverity() {
        return severity_.load(fbl::memory_order_relaxed);
    }

    void ActivateFallback(int fallback_fd);

private:
    zx_status_t VLogWrite(fx_log_severity_t severity, const char* tag,
                          const char* format, va_list args, bool perform_format);

    zx_status_t VLogWriteToSocket(fx_log_severity_t severity, const char* tag,
                                  const char* msg, va_list args, bool perform_format);

    zx_status_t VLogWriteToFd(int fd, fx_log_severity_t severity, const char* tag,
                              const char* msg, va_list args, bool perform_format);

    zx_status_t AddTags(const char** tags, size_t ntags);

    zx_koid_t pid_;
    fbl::atomic<fx_log_severity_t> severity_;
    fbl::atomic<uint32_t> dropped_logs_;
    fbl::atomic<int> logger_fd_;
    zx::socket socket_;
    fbl::Vector<fbl::String> tags_;

    // This field is just used to close fd when
    // logger object goes out of scope
    fbl::unique_fd fd_to_close_;

    // string representation to print in fallback mode
    fbl::String tagstr_;

    fbl::Mutex fallback_mutex_;
};

#endif // ZIRCON_SYSTEM_ULIB_SYSLOG_FX_LOGGER_H_
