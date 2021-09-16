// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/socket.h>
#include <zircon/types.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/pointer.h>

#include "log_decoder.h"

namespace log_decoder {
namespace {

TEST(LogDecoder, DecodesCorrectly) {
  syslog_backend::LogBuffer buffer;
  zx::socket logger_socket, our_socket;
  zx::socket::create(ZX_SOCKET_DATAGRAM, &logger_socket, &our_socket);
  syslog_backend::BeginRecordWithSocket(&buffer, syslog::LOG_INFO, __FILE__, __LINE__,
                                        "test message", nullptr, logger_socket.release());
  syslog_backend::WriteKeyValue(&buffer, "tag", "some tag");
  syslog_backend::WriteKeyValue(&buffer, "tag", "some other tag");
  syslog_backend::WriteKeyValue(&buffer, "user property", 5.2);
  syslog_backend::EndRecord(&buffer);
  syslog_backend::FlushRecord(&buffer);
  uint8_t data[2048];
  size_t processed = 0;
  our_socket.read(0, data, sizeof(data), &processed);
  const char* json = fuchsia_decode_log_message_to_json(data, sizeof(data));

  rapidjson::Document d;
  d.Parse(json);
  auto& entry = d[rapidjson::SizeType(0)];
  auto& tags = entry["metadata"]["tags"];
  auto& payload = entry["payload"]["root"];
  auto& keys = payload["keys"];
  ASSERT_EQ(tags[0].GetString(), std::string("some tag"));
  ASSERT_EQ(tags[1].GetString(), std::string("some other tag"));
  ASSERT_EQ(keys["user property"].GetDouble(), 5.2);
  ASSERT_EQ(payload["message"]["value"].GetString(), std::string("test message"));
  fuchsia_free_decoded_log_message(json);
}

}  // namespace
}  // namespace log_decoder
