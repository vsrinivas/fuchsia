// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/output_collector.h"

#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/zx/socket.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdio>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>

namespace run {

extern const size_t OC_DATA_BUFFER_SIZE = 2048;
extern const size_t OC_BUFFER_THRESHOLD = OC_DATA_BUFFER_SIZE * 2 - 1;

std::unique_ptr<OutputCollector> OutputCollector::Create() {
  zx::socket log_socket, server;
  ZX_ASSERT(ZX_OK == zx::socket::create(ZX_SOCKET_STREAM, &log_socket, &server));

  return std::make_unique<OutputCollector>(std::move(log_socket), std::move(server));
}

OutputCollector::OutputCollector(zx::socket log_socket, zx::socket server_socket)
    : callback_(nullptr),
      server_socket_(std::move(server_socket)),
      log_socket_(std::move(log_socket)),
      wait_(this, log_socket_.get(), ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READABLE) {}

void OutputCollector::CollectOutput(OutputCallBack callback, async_dispatcher_t* dispatcher) {
  ZX_ASSERT_MSG(callback_ == nullptr, "this function should only be called once.");
  ZX_ASSERT_MSG(log_socket_.is_valid(), "this function should only be called once.");

  callback_ = std::move(callback);
  ZX_ASSERT(ZX_OK == wait_.Begin(dispatcher));
}

void OutputCollector::Close() {
  if (callback_ && !buf_.empty()) {
    // last output line did not have '\n', write it.
    std::string s(buf_.begin(), buf_.end());
    callback_(std::move(s));
    buf_.clear();
  }
  wait_.Cancel();
  log_socket_.reset();
  for (auto& b : done_signals_) {
    b.completer.complete_ok();
  }
  done_signals_.clear();
  callback_ = nullptr;
}

fpromise::promise<> OutputCollector::SignalWhenDone() {
  fpromise::bridge<> bridge;
  auto promise = bridge.consumer.promise();
  if (!log_socket_.is_valid()) {
    bridge.completer.complete_ok();
  } else {
    done_signals_.push_back(std::move(bridge));
  }
  return promise;
}

OutputCollector::~OutputCollector() { Close(); }

void OutputCollector::Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                              zx_status_t status, const zx_packet_signal_t* signal) {
  ZX_ASSERT_MSG(ZX_OK == status, "Error occurred while collecting output: %s",
                zx_status_get_string(status));
  if (signal->observed & ZX_SOCKET_READABLE) {
    std::vector<uint8_t> data(OC_DATA_BUFFER_SIZE);
    zx_status_t status;

    while (true) {
      size_t len;
      status = log_socket_.read(0, data.data(), data.size(), &len);

      if (status == ZX_ERR_SHOULD_WAIT) {
        ZX_ASSERT(ZX_OK == wait->Begin(dispatcher));
        return;
      }
      if (status == ZX_ERR_PEER_CLOSED) {
        Close();
        return;
      }

      ZX_ASSERT_MSG(ZX_OK == status, "Error occurred while collecting output: %s",
                    zx_status_get_string(status));

      if (len == 0) {
        ZX_ASSERT(ZX_OK == wait->Begin(dispatcher));
        return;
      }

      // Flush buffer up to the last '\n' seen or up to a max threshold. In case of threshold we
      // can have interleaving prints but that is fine as test is choosing to print so much data to
      // stdout.
      auto last_newline = std::find(data.rbegin() + (data.size() - len), data.rend(), '\n');
      if (last_newline != data.rend()) {
        // print till new line.
        auto iter_end = data.begin() + (data.rend() - last_newline);
        std::string s(buf_.begin(), buf_.end());
        s.append(data.begin(), iter_end);
        callback_(std::move(s));
        buf_.clear();
        // put rest in buffer
        buf_.insert(buf_.end(), iter_end, data.begin() + len);
      } else if (buf_.size() + len > OC_BUFFER_THRESHOLD) {
        std::string s(buf_.begin(), buf_.end());
        s.append(data.begin(), data.end());
        callback_(std::move(s));
        buf_.clear();
      } else {
        buf_.insert(buf_.end(), data.begin(), data.begin() + len);
      }
      if (len != OC_DATA_BUFFER_SIZE) {
        // lazy read
        ZX_ASSERT(ZX_OK == wait->Begin(dispatcher));
        return;
      }

      // eagerly read only when last read filled full buffer.
    }
  }
  ZX_ASSERT(signal->observed & ZX_SOCKET_PEER_CLOSED);
  Close();
}

}  // namespace run
