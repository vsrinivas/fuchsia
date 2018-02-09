// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <zircon/assert.h>

#include <syslog/logger.h>
#include <syslog/wire_format.h>

#include "fx_logger.h"

namespace {

// This thread's koid.
// Initialized on first use.
thread_local zx_koid_t tls_thread_koid{ZX_KOID_INVALID};

zx_koid_t GetCurrentThreadKoid() {
    if (unlikely(tls_thread_koid == ZX_KOID_INVALID)) {
        tls_thread_koid = GetKoid(zx::thread::self().get());
    }
    ZX_DEBUG_ASSERT(tls_thread_koid != ZX_KOID_INVALID);
    return tls_thread_koid;
}

} // namespace

zx_status_t fx_logger::VLogWriteToSocket(fx_log_severity_t severity,
                                         const char* tag, const char* msg,
                                         va_list args) {
    zx_time_t time = zx_clock_get(ZX_CLOCK_MONOTONIC);
    fx_log_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    constexpr size_t kDataSize = sizeof(packet.data);
    packet.metadata.version = 0;
    packet.metadata.pid = pid_;
    packet.metadata.tid = GetCurrentThreadKoid();
    packet.metadata.time = time;
    packet.metadata.severity = severity;
    packet.metadata.dropped_logs = dropped_logs_.load();

    // Write tags
    size_t pos = 0;
    for (size_t i = 0; i < tags_.size(); i++) {
        size_t len = tags_[i].length();
        ZX_DEBUG_ASSERT(len < 128);
        packet.data[pos++] = static_cast<char>(len);
        memcpy(packet.data + pos, tags_[i].c_str(), len);
        pos += len;
    }
    if (tag != NULL) {
        size_t len = strlen(tag);
        if (len > 0) {
            size_t write_len =
                fbl::min(len, static_cast<size_t>(FX_LOG_MAX_TAG_LEN - 1));
            ZX_DEBUG_ASSERT(write_len < 128);
            packet.data[pos++] = static_cast<char>(write_len);
            memcpy(packet.data + pos, tag, write_len);
            pos += write_len;
        }
    }
    packet.data[pos++] = 0;
    ZX_DEBUG_ASSERT(pos < kDataSize);
    // Write msg
    int n = static_cast<int>(kDataSize - pos);
    int count = vsnprintf(packet.data + pos, n, msg, args);
    if (count < 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (count >= n) {
        // truncated
        constexpr char kEllipsis[] = "...";
        constexpr size_t kEllipsisSize = sizeof(kEllipsis) - 1;
        memcpy(packet.data + kDataSize - 1 - kEllipsisSize, kEllipsis,
               kEllipsisSize);
    }
    auto status = socket_.write(0, &packet, sizeof(packet), nullptr);
    switch (status) {
    case ZX_ERR_SHOULD_WAIT:
    case ZX_ERR_PEER_CLOSED:
    case ZX_ERR_NO_MEMORY:
    case ZX_ERR_BAD_STATE:
    case ZX_ERR_ACCESS_DENIED:
        dropped_logs_.fetch_add(1);
        break;
    }
    return status;
}

zx_status_t fx_logger::VLogWriteToConsoleFd(fx_log_severity_t severity,
                                            const char* tag, const char* msg,
                                            va_list args) {
    zx_time_t time = zx_clock_get(ZX_CLOCK_MONOTONIC);
    constexpr char kEllipsis[] = "...";
    constexpr size_t kEllipsisSize = sizeof(kEllipsis) - 1;
    constexpr size_t kMaxMessageSize = 2043;

    fbl::StringBuffer<kMaxMessageSize + kEllipsisSize + 1 /*\n*/> buf;
    buf.AppendPrintf("[%ld]", time);
    buf.AppendPrintf("[%ld]", pid_);
    buf.AppendPrintf("[%ld]", GetCurrentThreadKoid());

    buf.Append("[");
    if (!tagstr_.empty()) {
        buf.Append(tagstr_);
    }

    if (tag != NULL) {
        size_t len = strlen(tag);
        if (len > 0) {
            if (!tagstr_.empty()) {
                buf.Append(", ");
            }
            buf.Append(tag,
                       fbl::min(len, static_cast<size_t>(FX_LOG_MAX_TAG_LEN - 1)));
        }
    }
    buf.Append("]");
    switch (severity) {
    case FX_LOG_DEBUG:
        buf.Append(" DEBUG");
        break;
    case FX_LOG_INFO:
        buf.Append(" INFO");
        break;
    case FX_LOG_WARNING:
        buf.Append(" WARNING");
        break;
    case FX_LOG_ERROR:
        buf.Append(" ERROR");
        break;
    case FX_LOG_FATAL:
        buf.Append(" FATAL");
        break;
    default:
        buf.AppendPrintf(" VLOG(%d)", -severity);
    }
    buf.Append(": ");

    buf.AppendVPrintf(msg, args);
    if (buf.size() > kMaxMessageSize) {
        buf.Resize(kMaxMessageSize);
        buf.Append(kEllipsis);
    }
    buf.Append('\n');
    ssize_t status = write(console_fd_.get(), buf.data(), buf.size());
    if (status < 0) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t fx_logger::VLogWrite(fx_log_severity_t severity, const char* tag,
                                 const char* msg, va_list args) {
    if (msg == NULL || severity > FX_LOG_FATAL) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (GetSeverity() > severity) {
        return ZX_OK;
    }

    zx_status_t status;
    if (socket_.is_valid()) {
        status = VLogWriteToSocket(severity, tag, msg, args);
    } else if (console_fd_.get() != -1) {
        status = VLogWriteToConsoleFd(severity, tag, msg, args);
    } else {
        return ZX_ERR_BAD_STATE;
    }
    if (severity == FX_LOG_FATAL) {
        abort();
    }
    return status;
}

// This function is not thread safe
zx_status_t fx_logger::AddTags(const char** tags, size_t ntags) {
    if (ntags > FX_LOG_MAX_TAGS) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto fd_mode = console_fd_.get() != -1;
    for (size_t i = 0; i < ntags; i++) {
        auto len = strlen(tags[i]);
        fbl::String str(
            tags[i], len > FX_LOG_MAX_TAG_LEN - 1 ? FX_LOG_MAX_TAG_LEN - 1 : len);
        if (fd_mode) {
            if (tagstr_.empty()) {
                tagstr_ = str;
            } else {
                tagstr_ = fbl::String::Concat({tagstr_, ", ", str});
            }
        } else {
            tags_.push_back(str);
        }
    }
    return ZX_OK;
}
