// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "local_roughtime_server.h"
#include "settable_time_source.h"
#include "third_party/roughtime/protocol.h"
#include "third_party/roughtime/simple_server.h"
#include "third_party/roughtime/udp_processor.h"

namespace time_server {

using roughtime::Identity;
using roughtime::SimpleServer;
using roughtime::TimeSource;

LocalRoughtimeServer::LocalRoughtimeServer(
    SettableTimeSource* time_source,
    std::unique_ptr<SimpleServer> simple_server, uint16_t port_number)
    : time_source_(time_source),
      simple_server_(std::move(simple_server)),
      port_number_(port_number),
      is_running_(false) {}

// static
std::unique_ptr<LocalRoughtimeServer> LocalRoughtimeServer::MakeInstance(
    const uint8_t private_key[roughtime::kPrivateKeyLength],
    uint16_t preferred_port_number,
    roughtime::rough_time_t initial_time_micros) {
  constexpr roughtime::rough_time_t min_time_micros = 0;
  constexpr roughtime::rough_time_t max_time_micros =
      std::numeric_limits<uint64_t>::max();
  auto identity =
      SimpleServer::MakeIdentity(private_key, min_time_micros, max_time_micros);
  auto time_source = std::make_unique<SettableTimeSource>(initial_time_micros);
  // Capture a regular pointer here because |simple_server| needs to own the
  // unique_ptr.
  SettableTimeSource* time_source_ptr = time_source.get();

  int fd;
  uint16_t actual_port = 0;
  roughtime::UdpProcessor::MakeSocket(preferred_port_number, &fd, &actual_port);
  EXPECT_NE(actual_port, 0);
  FXL_LOG(INFO) << "Starting LocalRoughtimeServer on port " << actual_port;

  std::unique_ptr<SimpleServer> simple_server = std::make_unique<SimpleServer>(
      std::move(identity), std::move(time_source), fd);

  // Using |new| instead of |make_unique| because constructor is private
  return std::unique_ptr<LocalRoughtimeServer>(new LocalRoughtimeServer(
      time_source_ptr, std::move(simple_server), actual_port));
}

void LocalRoughtimeServer::Start() {
  is_running_ = true;
  while (IsRunning()) {
    simple_server_->ProcessBatch();
  }
}

void LocalRoughtimeServer::Stop() { is_running_ = false; }

bool LocalRoughtimeServer::IsRunning() { return is_running_; }

void LocalRoughtimeServer::SetTime(roughtime::rough_time_t server_time_micros) {
  EXPECT_NE(time_source_, nullptr);
  time_source_->SetTime(server_time_micros);
}

void LocalRoughtimeServer::SetTime(uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hour, uint8_t min, uint8_t sec) {
  struct tm time = {.tm_year = year - 1900,
                    .tm_mon = month - 1,
                    .tm_mday = day,
                    .tm_hour = hour,
                    .tm_min = min,
                    .tm_sec = sec};
  time_t epoch_seconds = timegm(&time);
  SetTime(static_cast<roughtime::rough_time_t>(epoch_seconds * 1'000'000));
}

uint16_t LocalRoughtimeServer::GetPortNumber() const { return port_number_; }

}  // namespace time_server
