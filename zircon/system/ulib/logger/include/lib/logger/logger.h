// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_LOGGER_LOGGER_H_
#define LIB_LOGGER_LOGGER_H_

#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/syslog/wire_format.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <stdint.h>

#include <memory>
#include <utility>

#include <fbl/function.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/string.h>
#include <fbl/vector.h>

namespace logger {

class LoggerImpl : public fbl::SinglyLinkedListable<std::unique_ptr<LoggerImpl>> {
 public:
  using ErrorCallback = fbl::Function<void(zx_status_t)>;

  explicit LoggerImpl(zx::channel channel, int out_fd);
  ~LoggerImpl();

  zx_status_t Begin(async_dispatcher_t* dispatcher);

  void set_error_handler(ErrorCallback error_handler) { error_handler_ = std::move(error_handler); }

  LoggerImpl* GetKey() const { return const_cast<LoggerImpl*>(this); }
  static size_t GetHash(const LoggerImpl* impl) { return reinterpret_cast<uintptr_t>(impl); }

 private:
  void OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);
  void OnLogMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                    const zx_packet_signal_t* signal);
  zx_status_t ReadAndDispatchMessage(fidl::MessageBuffer* buffer, async_dispatcher_t* dispatcher);

  zx_status_t Connect(fidl::HLCPPIncomingMessage message, async_dispatcher_t* dispatcher);

  zx_status_t PrintLogMessage(const fx_log_packet_t* packet);

  void NotifyError(zx_status_t error);

  zx::channel channel_;
  zx::socket socket_;
  int fd_;
  async::WaitMethod<LoggerImpl, &LoggerImpl::OnHandleReady> wait_;
  async::WaitMethod<LoggerImpl, &LoggerImpl::OnLogMessage> socket_wait_;
  ErrorCallback error_handler_;
};

}  // namespace logger

#endif  // LIB_LOGGER_LOGGER_H_
