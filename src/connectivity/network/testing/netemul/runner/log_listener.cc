// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_listener.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/logger.h>
#include <lib/syslog/wire_format.h>
#include <src/lib/fxl/logging.h>
#include <zircon/status.h>

#include "format.h"

namespace netemul {

namespace internal {

/*
 * LogListenerImpl
 *
 * public
 */

LogListenerImpl::LogListenerImpl(
    fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
    std::string prefix, std::ostream* stream, bool klogs_enabled,
    async_dispatcher_t* dispatcher, zx::socket log_sink)
    : binding_(this, std::move(request), dispatcher),
      prefix_(std::move(prefix)),
      stream_(stream),
      dropped_logs_(0),
      klogs_enabled_(klogs_enabled) {
  binding_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "LogListenerImpl error: " << zx_status_get_string(status)
                   << std::endl;
  });
  if (!stream_) {
    // modify the prefix when logging to syslog
    prefix_ = "@" + prefix_;
    if (log_sink.is_valid()) {
      log_sock_ = std::move(log_sink);
    } else {
      auto ctx = sys::ComponentContext::Create();
      fuchsia::logger::LogSinkPtr log;
      ctx->svc()->Connect(log.NewRequest(dispatcher));
      zx::socket out;
      zx::socket::create(ZX_SOCKET_DATAGRAM, &out, &log_sock_);
      log->Connect(std::move(out));
    }
  }
}

void LogListenerImpl::Log(fuchsia::logger::LogMessage m) {
  // TODO(ghanan): Filter out kernel logs before reaching here.
  // Ignore kernel logs
  if (!klogs_enabled_ &&
      std::find(m.tags.begin(), m.tags.end(), "klog") != m.tags.end()) {
    return;
  }

  if (stream_) {
    *stream_ << "[" << prefix_ << "]";
    FormatTime(m.time);
    *stream_ << "[" << m.pid << "]"
             << "[" << m.tid << "]";
    FormatTags(m.tags);
    FormatLogLevel(m.severity);
    *stream_ << " " << m.msg << std::endl;
  } else {
    // if we don't have a stream, we're just going to append environment tags
    // to the system log.
    fx_log_packet_t packet;
    packet.metadata.dropped_logs = dropped_logs_;
    packet.metadata.pid = m.pid;
    packet.metadata.tid = m.tid;
    packet.metadata.severity = m.severity;
    packet.metadata.time = m.time;
    bool inline_prefix = true;
    if (m.tags.size() < FX_LOG_MAX_TAGS) {
      m.tags.push_back(prefix_);
      inline_prefix = false;
    }

    // insert tags:
    size_t pos = 0;
    for (const auto& tag : m.tags) {
      auto len = tag.length();
      if (len > FX_LOG_MAX_TAG_LEN - 1) {
        len = FX_LOG_MAX_TAG_LEN - 1;
      }
      packet.data[pos] = len;
      pos++;
      memcpy(&packet.data[pos], tag.c_str(), len);
      pos += len;
    }
    packet.data[pos++] = 0;
    if (inline_prefix && prefix_.length() + 4 + pos < sizeof(packet.data)) {
      packet.data[pos++] = '[';
      memcpy(&packet.data[pos], prefix_.c_str(), prefix_.length());
      pos += prefix_.length();
      packet.data[pos++] = ']';
      packet.data[pos++] = ' ';
    }
    ZX_ASSERT(pos <= sizeof(packet.data));
    auto remain = sizeof(packet.data) - pos;
    if (m.msg.length() + 1 < remain) {
      remain = m.msg.length() + 1;
    }
    if (remain > 0) {
      memcpy(&packet.data[pos], m.msg.c_str(), remain);
      packet.data[pos + remain - 1] = 0;
      pos += remain;
    }

    size_t actual;
    pos += sizeof(packet.metadata);
    if (log_sock_.write(0, &packet, pos, &actual) == ZX_OK && actual == pos) {
      dropped_logs_ = 0;
    } else {
      dropped_logs_++;
    }
  }
}

void LogListenerImpl::LogMany(std::vector<fuchsia::logger::LogMessage> ms) {
  for (auto& m : ms) {
    Log(std::move(m));
  }
}

void LogListenerImpl::Done() {
  FXL_LOG(INFO) << "DONE";
  return;
}

/*
 * LogListenerImpl
 *
 * private
 */

void LogListenerImpl::FormatTime(const zx_time_t timestamp) {
  internal::FormatTime(stream_, timestamp);
}

void LogListenerImpl::FormatTags(const std::vector<std::string>& tags) {
  auto it = tags.begin();

  *stream_ << "[";

  while (it != tags.end()) {
    *stream_ << *it;

    it = std::next(it);

    if (it != tags.end()) {
      *stream_ << ",";
    }
  }

  *stream_ << "]";
}

void LogListenerImpl::FormatLogLevel(const int32_t severity) {
  switch (severity) {
    case 0:
      *stream_ << "[INFO]";
      break;

    case 1:
      *stream_ << "[WARNING]";
      break;

    case 2:
      *stream_ << "[ERROR]";
      break;

    case 3:
      *stream_ << "[FATAL]";
      break;

    default:
      if (severity > 3) {
        *stream_ << "[INVALID]";
      } else {
        *stream_ << "[VLOG(" << severity << ")]";
      }
  }
}

}  // namespace internal

/*
 * LogListener
 *
 * public
 */

LogListener::LogListener(std::unique_ptr<internal::LogListenerImpl> impl)
    : loglistener_impl_(std::move(impl)) {}

std::unique_ptr<LogListener> LogListener::Create(
    fuchsia::netemul::environment::ManagedEnvironment* env,
    const fuchsia::netemul::environment::LoggerOptions& logger_options,
    const std::string& prefix, async_dispatcher_t* dispatcher) {
  if (!logger_options.has_enabled() || !logger_options.enabled()) {
    return nullptr;
  }

  // Create the client side interface and connect to a remote
  // that implementes the fuchsia.logger.Log interface within
  // `env`.
  fuchsia::logger::LogPtr log_service;
  log_service.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "LogListenerImpl error: " << zx_status_get_string(status)
                   << std::endl;
  });

  env->ConnectToService(fuchsia::logger::Log::Name_,
                        log_service.NewRequest().TakeChannel());

  bool klogs_enabled =
      logger_options.has_klogs_enabled() && logger_options.klogs_enabled();

  auto* stream =
      logger_options.has_syslog_output() && logger_options.syslog_output()
          ? nullptr
          : &std::cout;

  // Create an instance of the LogListener implementation
  // and start listening for logs
  fidl::InterfaceHandle<fuchsia::logger::LogListener> loglistener_h;
  std::unique_ptr<internal::LogListenerImpl> impl(new internal::LogListenerImpl(
      loglistener_h.NewRequest(), prefix, stream, klogs_enabled, dispatcher));

  if (logger_options.has_filter_options()) {
    log_service->Listen(std::move(loglistener_h),
                        std::make_unique<fuchsia::logger::LogFilterOptions>(
                            logger_options.filter_options()));
  } else {
    log_service->Listen(std::move(loglistener_h), nullptr);
  }

  return std::make_unique<LogListener>(std::move(impl));
}

}  // namespace netemul
