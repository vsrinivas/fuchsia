// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_logger.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iostream>

#include "format.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace netemul {

static std::string ExtractLabel(const std::string& url) {
  size_t last_slash = url.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 == url.length())
    return url;
  return url.substr(last_slash + 1);
}

ManagedLogger::ManagedLogger(std::string name, bool is_err,
                             std::shared_ptr<fuchsia::logger::LogListenerSafe> loglistener)
    : dispatcher_(async_get_default_dispatcher()),
      name_(std::move(name)),
      is_err_(is_err),
      wait_(this),
      loglistener_(std::move(loglistener)) {}

ManagedLogger::~ManagedLogger() {
  // Consume all pending data from the socket.
  while (ReadSocket() == ZX_OK) {
  }
  // Publish any outstanding data as a message (in case it doesn't end with a newline).
  if (buffer_pos_ != 0) {
    buffer_[buffer_pos_] = 0x00;
    LogMessage(buffer_.get());
  }
}

zx::handle ManagedLogger::CreateHandle() {
  ZX_ASSERT(!out_.is_valid());
  zx::socket ret;
  zx::socket::create(0, &ret, &out_);
  return zx::handle(ret.release());
}

void ManagedLogger::OnRx(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                         const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Managed Logger wait failed: " << zx_status_get_string(status);
    closed_callback_(this);
    return;
  }

  if (signal->observed & ZX_SOCKET_READABLE) {
    status = ReadSocket();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Managed Logger read failed: " << zx_status_get_string(status);
      closed_callback_(this);
      return;
    }
  }

  if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
    closed_callback_(this);
  } else {
    // wait for next
    wait_.Begin(dispatcher_);
  }
}

zx_status_t ManagedLogger::ReadSocket() {
  if (!buffer_) {
    buffer_.reset(new char[BufferSize]);
    buffer_pos_ = 0;
  }

  size_t actual;
  zx_status_t status =
      out_.read(0, buffer_.get() + buffer_pos_, BufferSize - 1 - buffer_pos_, &actual);
  if (status != ZX_OK) {
    return status;
  }
  buffer_pos_ += actual;
  buffer_[buffer_pos_] = 0x00;
  ConsumeBuffer();

  return ZX_OK;
}

void ManagedLogger::ConsumeBuffer() {
  // Do nothing if we dont have a log listener to output to
  if (!loglistener_) {
    buffer_pos_ = 0;
    return;
  }

  // find newlines and print:
  char* ptr = buffer_.get();
  char* end = buffer_.get() + buffer_pos_;
  char* full = buffer_.get() + (BufferSize - 1);
  char* mark = ptr;

  while (ptr != end) {
    while (*ptr != '\n' && ptr != end) {
      ptr++;
    }
    // newline found, print it
    if (*ptr == '\n') {
      *ptr = 0x00;
      LogMessage(mark);
      mark = ptr + 1;
    }
  }

  if (mark != buffer_.get()) {
    // if mark is not at the beginning of the buffer
    buffer_pos_ = 0;
    ptr = buffer_.get();
    // copy what wasn't printed to the beginning of the buffer
    while (mark != end) {
      *ptr++ = *mark++;
      buffer_pos_++;
    }
  } else if (ptr == full) {
    // buffer filled up, just print everything
    *ptr = 0x00;
    LogMessage(buffer_.get());
    buffer_pos_ = 0;
  }
}

void ManagedLogger::LogMessage(std::string msg) {
  fuchsia::logger::LogMessage m;

  m.pid = 0;
  m.tid = 0;
  m.time = zx_clock_get_monotonic();
  m.severity = (int32_t)(is_err_ ? fuchsia::logger::LogLevelFilter::ERROR
                                 : fuchsia::logger::LogLevelFilter::INFO);
  m.dropped_logs = 0;
  m.tags = std::vector<std::string>{name_};
  m.msg = std::move(msg);

  // Should never end up here if loglistener_ is a nullptr
  ZX_ASSERT(loglistener_);
  loglistener_->Log(std::move(m), []() {});
}

void ManagedLogger::Start(netemul::ManagedLogger::ClosedCallback callback) {
  ZX_ASSERT(out_.is_valid());
  closed_callback_ = std::move(callback);
  wait_.set_object(out_.get());
  wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);
  wait_.Begin(dispatcher_);
}

fuchsia::sys::FileDescriptorPtr ManagedLoggerCollection::CreateLogger(const std::string& url,
                                                                      bool err) {
  auto& logger = loggers_.emplace_back(
      std::make_unique<ManagedLogger>(ExtractLabel(url).c_str(), err, loglistener_));

  auto handle = logger->CreateHandle();
  logger->Start([this](ManagedLogger* logger) {
    for (auto i = loggers_.begin(); i != loggers_.end(); i++) {
      if (i->get() == logger) {
        loggers_.erase(i);
        break;
      }
    }
  });
  auto ret = fuchsia::sys::FileDescriptor::New();
  ret->handle0 = std::move(handle);
  ret->type0 = PA_FD;
  return ret;
}

}  // namespace netemul
