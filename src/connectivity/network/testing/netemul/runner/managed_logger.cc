// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_logger.h"

#include <lib/async/default.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <zircon/assert.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iostream>

#include "format.h"

namespace netemul {

static constexpr int BufferSize = 4096;
static const char* kLogEnvNamePrefix = "[%s]";
static const char* kLogPrefixOut = "[%s:%d] ";
static const char* kLogPrefixErr = "[%s:%d][err] ";

static std::string ExtractLabel(const std::string& url) {
  size_t last_slash = url.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 == url.length())
    return url;
  return url.substr(last_slash + 1);
}

ManagedLogger::ManagedLogger(std::string env_name, std::string prefix,
                             std::ostream* stream)
    : dispatcher_(async_get_default_dispatcher()),
      stream_(stream),
      env_name_(std::move(env_name)),
      prefix_(std::move(prefix)),
      wait_(this) {}

zx::handle ManagedLogger::CreateHandle() {
  ZX_ASSERT(!out_.is_valid());
  zx::socket ret;
  zx::socket::create(0, &ret, &out_);
  return zx::handle(ret.release());
}

void ManagedLogger::OnRx(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                         zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Managed Logger wait failed: "
                   << zx_status_get_string(status);
    closed_callback_(this);
    return;
  }

  if (signal->observed & ZX_SOCKET_READABLE) {
    // lazily allocate buffer if not created:
    if (!buffer_) {
      buffer_.reset(new char[BufferSize]);
      buffer_pos_ = 0;
    }

    size_t actual;
    status = out_.read(0, buffer_.get() + buffer_pos_,
                       BufferSize - 1 - buffer_pos_, &actual);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Managed Logger read failed: "
                     << zx_status_get_string(status);
      closed_callback_(this);
      return;
    }
    buffer_pos_ += actual;
    buffer_[buffer_pos_] = 0x00;
    ConsumeBuffer();
  }

  if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
    closed_callback_(this);
  } else {
    // wait for next
    wait_.Begin(dispatcher_);
  }
}

void ManagedLogger::ConsumeBuffer() {
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
      (*stream_) << env_name_;
      FormatTime();
      (*stream_) << prefix_ << mark << std::endl;
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
    (*stream_) << env_name_;
    FormatTime();
    (*stream_) << prefix_ << buffer_.get() << std::endl;
    buffer_pos_ = 0;
  }
}

void ManagedLogger::FormatTime() {
  zx_time_t timestamp = zx_clock_get_monotonic();
  internal::FormatTime(stream_, timestamp);
}

void ManagedLogger::Start(netemul::ManagedLogger::ClosedCallback callback) {
  ZX_ASSERT(out_.is_valid());
  closed_callback_ = std::move(callback);
  wait_.set_object(out_.get());
  wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);
  wait_.Begin(dispatcher_);
}

fuchsia::sys::FileDescriptorPtr ManagedLoggerCollection::CreateLogger(
    const std::string& url, bool err) {
  auto env_name =
      fxl::StringPrintf(kLogEnvNamePrefix, environment_name_.c_str());
  auto prefix = fxl::StringPrintf(err ? kLogPrefixErr : kLogPrefixOut,
                                  ExtractLabel(url).c_str(), counter_);

  auto* stream = err ? &std::cerr : &std::cout;
  auto& logger = loggers_.emplace_back(std::make_unique<ManagedLogger>(
      std::move(env_name), std::move(prefix), stream));
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
