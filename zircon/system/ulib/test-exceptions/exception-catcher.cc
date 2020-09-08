// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/test-exceptions/exception-catcher.h>
#include <lib/zx/object.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/syscalls/debug.h>

#include <utility>

namespace test_exceptions {

namespace {

template <typename T>
zx_status_t GetKoid(const zx::object<T>& object, zx_koid_t* koid) {
  zx_info_handle_basic_t info;
  zx_status_t status = object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    *koid = ZX_KOID_INVALID;
    return status;
  }
  *koid = info.koid;
  return ZX_OK;
}

// Returns true if |info| matches the given koids, with ZX_KOID_INVALID
// matching anything.
bool ExceptionMatches(const zx_exception_info_t& info, zx_koid_t pid, zx_koid_t tid) {
  return (pid == ZX_KOID_INVALID || pid == info.pid) && (tid == ZX_KOID_INVALID || tid == info.tid);
}

}  // namespace

__EXPORT
ExceptionCatcher::~ExceptionCatcher() {
  zx_status_t status = Stop();
  ZX_ASSERT_MSG(status == ZX_OK, "ExceptionCatcher::Stop() failed (%s)",
                zx_status_get_string(status));
}

__EXPORT
zx_status_t ExceptionCatcher::Stop() {
  // Move these to local vars so they always get cleared when we return.
  zx::channel exception_channel = std::move(exception_channel_);
  auto active_exceptions = std::move(active_exceptions_);

  bool exceptions_in_channel = false;
  if (exception_channel) {
    zx_status_t status = exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time(0), nullptr);
    if (status == ZX_OK) {
      exceptions_in_channel = true;
    } else if (status != ZX_ERR_TIMED_OUT) {
      return status;
    }
  }

  return (active_exceptions.empty() && !exceptions_in_channel) ? ZX_OK : ZX_ERR_CANCELED;
}

__EXPORT
zx::status<zx::exception> ExceptionCatcher::ExpectException() {
  return ExpectException(ZX_KOID_INVALID, ZX_KOID_INVALID);
}

__EXPORT
zx::status<zx::exception> ExceptionCatcher::ExpectException(const zx::thread& thread) {
  zx_koid_t tid;
  zx_status_t status = GetKoid(thread, &tid);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return ExpectException(ZX_KOID_INVALID, tid);
}

__EXPORT
zx::status<zx::exception> ExceptionCatcher::ExpectException(const zx::process& process) {
  zx_koid_t pid;
  zx_status_t status = GetKoid(process, &pid);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return ExpectException(pid, ZX_KOID_INVALID);
}

zx::status<zx::exception> ExceptionCatcher::ExpectException(zx_koid_t pid, zx_koid_t tid) {
  // First check if we've already seen this exception on a previous call.
  for (auto iter = active_exceptions_.begin(); iter != active_exceptions_.end(); ++iter) {
    if (ExceptionMatches(iter->info, pid, tid)) {
      auto exception = zx::ok(std::move(iter->exception));
      active_exceptions_.erase(iter);
      return std::move(exception);
    }
  }

  while (1) {
    zx_signals_t signals = 0;
    zx_status_t status = exception_channel_.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                                     zx::time::infinite(), &signals);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    if (!(signals & ZX_CHANNEL_READABLE)) {
      return zx::error(ZX_ERR_PEER_CLOSED);
    }

    zx_exception_info_t info;
    zx::exception exception;
    status = exception_channel_.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                     nullptr, nullptr);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    if (ExceptionMatches(info, pid, tid)) {
      return zx::ok(std::move(exception));
    }
    active_exceptions_.push_back(ActiveException{info, std::move(exception)});
  }
}

}  // namespace test_exceptions
