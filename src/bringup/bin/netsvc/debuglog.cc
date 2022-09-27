// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/debuglog.h"

#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/clock.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <iomanip>
#include <sstream>

#include "src/bringup/bin/netsvc/inet6.h"

constexpr zx::duration kSendDelayShort = zx::msec(100);
constexpr zx::duration kSendDelayLong = zx::sec(4);

// Number of consecutive unacknowledged packets we will send before reducing send rate.
constexpr unsigned kUnackedThreshold = 5;

LogListener::LogListener(async_dispatcher_t* dispatcher, SendFn send, bool retransmit,
                         size_t max_msg_size)
    : dispatcher_(dispatcher),
      retransmit_(retransmit),
      max_msg_size_(max_msg_size),
      send_delay_(kSendDelayShort),
      send_fn_(std::move(send)),
      timeout_task_([this](async_dispatcher_t* dispatcher, async::Task* task, zx_status_t status) {
        if (status == ZX_ERR_CANCELED) {
          return;
        }
        ZX_ASSERT_MSG(status == ZX_OK, "unexpected task status %s", zx_status_get_string(status));
        if (pkt_.has_value()) {
          // No reply. If no one is listening, reduce send rate.
          if (++num_unacked_ >= kUnackedThreshold) {
            send_delay_ = kSendDelayLong;
          }
        }
        MaybeSendLog();
      }) {
  ZX_ASSERT_MSG(max_msg_size <= MAX_LOG_DATA,
                "maximum message size %ld greater than max log data %d", max_msg_size,
                MAX_LOG_DATA);
}

void LogListener::Bind(fidl::ServerEnd<_EnclosingProtocol> server_end) {
  binding_.emplace(fidl::BindServer(
      dispatcher_, std::move(server_end), this,
      [](LogListener*, fidl::UnbindInfo info, fidl::ServerEnd<_EnclosingProtocol>) {
        if (info.status() != ZX_OK) {
          printf("netsvc: lost connection to system logs: %s\n", info.FormatDescription().c_str());
        }
      }));
}

LogListener::StagedPacket::StagedPacket(uint32_t seqno, const char* nodename,
                                        LogListener::PendingMessage&& msg)
    : contents({.magic = NB_DEBUGLOG_MAGIC, .seqno = seqno}),
      len(sizeof(contents) - (sizeof(contents.data) - msg.log_message.size())),
      message(std::move(msg)) {
  ZX_ASSERT_MSG(message.log_message.size() <= sizeof(contents.data), "invalid message size %lu",
                message.log_message.size());
  strncpy(contents.nodename, nodename, sizeof(contents.nodename) - 1);
  memcpy(contents.data, message.log_message.c_str(), message.log_message.size());
}

void LogListener::Log(LogRequestView request, LogCompleter::Sync& completer) {
  PushLogMessage(request->log);
  // Store completer.
  pending_.back().completer = completer.ToAsync();
  TryLoadNextPacket();
}

void LogListener::LogMany(LogManyRequestView request, LogManyCompleter::Sync& completer) {
  for (const fuchsia_logger::wire::LogMessage& message : request->log) {
    PushLogMessage(message);
  }
  // Store completer on last queued message.
  pending_.back().completer = completer.ToAsync();
  TryLoadNextPacket();
}

void LogListener::Done(DoneCompleter::Sync&) { ZX_PANIC("unexpected call to Done"); }

// Helpers to visit on pending message variants.
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

void LogListener::Ack(uint32_t seqno) {
  if (seqno != seqno_ || !pkt_.has_value()) {
    return;
  }
  // Received an ack. We have an active listener. Don't delay.
  num_unacked_ = 0;
  send_delay_ = kSendDelayShort;
  seqno_++;

  pkt_.value().message.Complete();
  pkt_.reset();
  TryLoadNextPacket();
}

void LogListener::PendingMessage::Complete() {
  std::visit(overloaded{[](std::monostate&) {},
                        [](LogCompleter::Async& c) {
                          c.Reply();
                          fidl::Status result = c.result_of_reply();
                          if (!result.ok()) {
                            printf("netsvc: failed to confirm logs: %s\n",
                                   result.FormatDescription().c_str());
                          }
                        },
                        [](LogManyCompleter::Async& c) {
                          c.Reply();
                          fidl::Status result = c.result_of_reply();
                          if (!result.ok()) {
                            printf("netsvc: failed to confirm logs: %s\n",
                                   result.FormatDescription().c_str());
                          }
                        }},
             completer);
}

void LogListener::PushLogMessage(const fuchsia_logger::wire::LogMessage& message) {
  std::stringstream ss;
  zx::duration timestamp(message.time);
  // Add time in format [secs.millis].
  ss << '[' << std::setw(5) << std::setfill('0') << (timestamp.to_secs());
  ss << '.' << std::setw(3) << std::setfill('0') << (timestamp.to_msecs() % 1000ULL) << ']';
  // Add PID and TID as "tid.pid".
  ss << ' ' << std::setw(5) << std::setfill('0') << message.pid;
  ss << '.' << std::setw(5) << std::setfill('0') << message.tid;

  // Add message tags.
  ss << " [";
  for (auto tag = message.tags.begin(); tag != message.tags.end(); tag++) {
    if (tag != message.tags.begin()) {
      ss << ',';
    }
    ss << tag->get();
  }
  ss << "] ";
  size_t size = ss.tellp();
  ZX_ASSERT_MSG(size < max_msg_size_, "message preamble too long: %ld", size);
  cpp17::string_view contents = message.msg.get();
  size = std::min(max_msg_size_ - size, contents.size());
  ss << contents.substr(0, size);
  if (static_cast<size_t>(ss.tellp()) < max_msg_size_) {
    ss << '\n';
    pending_.push(PendingMessage{.log_message = ss.str()});
    return;
  }
  pending_.push(PendingMessage{.log_message = ss.str()});

  contents = contents.substr(size);
  while (!contents.empty()) {
    size_t partial = std::min(max_msg_size_, contents.size());
    pending_.push(PendingMessage{.log_message = std::string(contents.substr(0, partial))});
    contents = contents.substr(partial);
  }
  PendingMessage& last = pending_.back();
  if (last.log_message.size() < max_msg_size_) {
    last.log_message += '\n';
  } else {
    pending_.push(PendingMessage{.log_message = "\n"});
  }
}

void LogListener::TryLoadNextPacket() {
  if (pkt_.has_value() || pending_.empty()) {
    return;
  }
  pkt_.emplace(seqno_, nodename(), std::move(pending_.front()));
  pending_.pop();
  MaybeSendLog();
}

void LogListener::MaybeSendLog() {
  if (!pkt_.has_value()) {
    return;
  }
  StagedPacket& staged = pkt_.value();
  send_fn_(staged.contents, staged.len);
  if (retransmit_) {
    zx_status_t status = timeout_task_.Cancel();
    ZX_ASSERT_MSG(status == ZX_OK || status == ZX_ERR_NOT_FOUND, "failed to cancel task %s",
                  zx_status_get_string(status));
    status = timeout_task_.PostDelayed(dispatcher_, send_delay_);
    ZX_ASSERT_MSG(status == ZX_OK, "failed to schedule timeout task %s",
                  zx_status_get_string(status));
  }
}

namespace {
std::optional<LogListener> gListener;
}

zx_status_t debuglog_init(async_dispatcher_t* dispatcher) {
  zx::status log_client_end = component::Connect<fuchsia_logger::Log>();
  if (log_client_end.is_error()) {
    return log_client_end.status_value();
  }
  fidl::WireSyncClient client{std::move(log_client_end.value())};

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_logger::LogListenerSafe>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }
  auto [client_end, server_end] = std::move(endpoints.value());
  {
    fidl::WireResult result = client->ListenSafe(
        std::move(client_end), fidl::ObjectView<fuchsia_logger::wire::LogFilterOptions>(nullptr));
    if (!result.ok()) {
      return result.status();
    }
  }

  auto& listener = gListener.emplace(
      dispatcher,
      [](const logpacket_t& pkt, size_t len) {
        zx_status_t status =
            udp6_send(&pkt, len, &ip6_ll_all_nodes, DEBUGLOG_PORT, DEBUGLOG_ACK_PORT, false);
        if (status != ZX_OK) {
          printf("netsvc: failed to send debuglog: %s\n", zx_status_get_string(status));
        }
      },
      /* retransmit */ true, MAX_LOG_DATA);
  listener.Bind(std::move(server_end));

  return ZX_OK;
}

void debuglog_recv(async_dispatcher_t* dispatcher, void* data, size_t len, bool is_mcast) {
  // The only message we should be receiving is acknowledgement of our last transmission
  if ((len != 8) || is_mcast) {
    return;
  }
  // Copied not cast in-place to satisfy alignment requirements flagged by ubsan (see
  // fxbug.dev/45798).
  logpacket_t pkt;
  memcpy(&pkt, data, sizeof(logpacket_t));
  if ((pkt.magic != NB_DEBUGLOG_MAGIC)) {
    return;
  }

  ZX_ASSERT_MSG(gListener.has_value(), "debuglog was not initialized");
  gListener.value().Ack(pkt.seqno);
}
