// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_
#define SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_

#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <zircon/boot/netboot.h>
#include <zircon/types.h>

#include <queue>

#include "src/bringup/bin/netsvc/netsvc.h"

zx_status_t debuglog_init(async_dispatcher_t* dispatcher);

void debuglog_recv(async_dispatcher_t* dispatcher, void* data, size_t len, bool is_mcast);

// Log listener is a fuchsia.logger.LogListenerSafe server and implements the
// netboot/debuglog protocol.
//
// Logs received over the FIDL protocol are serialized into netboot/debuglog
// packets. Acknowledgements over the network are used to complete the control
// flow loop with the fuchsia.logger.LogListenerSafe protocol.
class LogListener : public fidl::WireServer<fuchsia_logger::LogListenerSafe> {
 public:
  using SendFn = fit::function<void(const logpacket_t&, size_t)>;

  DISALLOW_COPY_ASSIGN_AND_MOVE(LogListener);
  LogListener(async_dispatcher_t* dispatcher, SendFn send, bool retransmit, size_t max_msg_size);

  void Bind(fidl::ServerEnd<_EnclosingProtocol> server_end);

  void Log(LogRequestView request, LogCompleter::Sync& completer) override;

  void LogMany(LogManyRequestView request, LogManyCompleter::Sync& completer) override;

  void Done(DoneRequestView request, DoneCompleter::Sync&) override;

  void Ack(uint32_t seqno);

 private:
  friend class LogListenerTestHelper;

  void PushLogMessage(const fuchsia_logger::wire::LogMessage& message);

  void TryLoadNextPacket();

  void MaybeSendLog();

  struct PendingMessage {
    std::string log_message;
    std::variant<std::monostate, LogCompleter::Async, LogManyCompleter::Async> completer;

    void Complete();
  };

  struct StagedPacket {
    logpacket_t contents;
    size_t len;
    PendingMessage message;

    StagedPacket(uint32_t seqno, const char* nodename, PendingMessage&& msg);
  };

  async_dispatcher_t* const dispatcher_;
  const bool retransmit_;
  const size_t max_msg_size_;
  zx::duration send_delay_;
  SendFn send_fn_;
  async::Task timeout_task_;
  uint32_t seqno_ = 1;
  size_t num_unacked_ = 0;
  std::optional<StagedPacket> pkt_;
  std::queue<PendingMessage> pending_;
  std::optional<fidl::ServerBindingRef<_EnclosingProtocol>> binding_;
};

#endif  // SRC_BRINGUP_BIN_NETSVC_DEBUGLOG_H_
