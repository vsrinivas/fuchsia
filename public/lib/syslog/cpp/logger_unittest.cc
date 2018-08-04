// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <lib/zx/socket.h>
#include <syslog/global.h>
#include <syslog/wire_format.h>

#include "gtest/gtest.h"
#include "lib/syslog/cpp/logger.h"

__BEGIN_CDECLS

// This does not come from header file as this function should only be used in
// tests and is not for general use.
void fx_log_reset_global(void);
__END_CDECLS

namespace {

class Cleanup {
 public:
  Cleanup() { fx_log_reset_global(); }
  ~Cleanup() { fx_log_reset_global(); }
};

bool ends_with(const char* str, const char* suffix) {
  if (strlen(str) < strlen(suffix)) {
    return false;
  }
  size_t l = strlen(suffix);
  str += strlen(str) - l;
  return strcmp(str, suffix) == 0;
}

inline zx_status_t init_helper(zx_handle_t handle, const char** tags,
                               size_t ntags) {
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = handle,
                               .tags = tags,
                               .num_tags = ntags};

  return fx_log_init_with_config(&config);
}

void output_compare_helper(zx::socket local, fx_log_severity_t severity,
                           const char* msg, const char** tags, int num_tags) {
  fx_log_packet_t packet;
  ASSERT_EQ(ZX_OK, local.read(0, &packet, sizeof(packet), nullptr));
  EXPECT_EQ(severity, packet.metadata.severity);
  int pos = 0;
  for (int i = 0; i < num_tags; i++) {
    const char* tag = tags[i];
    auto tag_len = static_cast<int8_t>(strlen(tag));
    ASSERT_EQ(tag_len, packet.data[pos]);
    pos++;
    ASSERT_STREQ(tag, packet.data + pos);
    pos += tag_len;
  }
  ASSERT_EQ(0, packet.data[pos]);
  pos++;
  EXPECT_TRUE(ends_with(packet.data + pos, msg)) << (packet.data + pos);
}

TEST(LogInit, Init) {
  Cleanup cleanup;
  ASSERT_EQ(ZX_OK, syslog::InitLogger());
  fx_log_reset_global();
  ASSERT_EQ(ZX_OK, syslog::InitLogger({"tag1", "tag2"}));
}

TEST(Logger, LogSimple) {
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0));
  const char* msg = "test message";
  FX_LOGS(INFO) << msg;
  output_compare_helper(std::move(local), FX_LOG_INFO, msg, nullptr, 0);
}

TEST(Logger, WithSeverityMacro) {
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0));
  const char* msg = "test message";
  FX_LOGS_WITH_SEVERITY(FX_LOG_INFO) << msg;
  output_compare_helper(std::move(local), FX_LOG_INFO, msg, nullptr, 0);
}

TEST(Logger, LogSeverity) {
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0));

  FX_VLOGS(1) << "just some msg";
  size_t outstanding_bytes = 10u;  // init to non zero value.
  ASSERT_EQ(ZX_OK, local.read(0, nullptr, 0, &outstanding_bytes));
  EXPECT_EQ(0u, outstanding_bytes);

  FX_LOGS(WARNING) << "just some msg";
  output_compare_helper(std::move(local), FX_LOG_WARNING, "just some msg",
                        nullptr, 0);
}

TEST(Logger, LogWithTag) {
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0));
  FX_LOGST(INFO, "tag") << "just some string";
  const char* tags[] = {"tag"};
  output_compare_helper(std::move(local), FX_LOG_INFO, "just some string", tags,
                        1);
}

TEST(Logger, CheckFunction) {
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0));

  FX_CHECK(1 > 0) << "error msg";
  size_t outstanding_bytes = 10u;  // init to non zero value.
  ASSERT_EQ(ZX_OK, local.read(0, nullptr, 0, &outstanding_bytes));
  EXPECT_EQ(0u, outstanding_bytes);
}

TEST(Logger, VLog) {
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0));
  const char* msg = "test message";
  FX_VLOGS(1) << msg;
  size_t outstanding_bytes = 10u;  // init to non zero value.
  ASSERT_EQ(ZX_OK, local.read(0, nullptr, 0, &outstanding_bytes));
  EXPECT_EQ(0u, outstanding_bytes);

  FX_LOG_SET_VERBOSITY(1);
  FX_VLOGS(2) << msg;
  outstanding_bytes = 10u;  // init to non zero value.
  ASSERT_EQ(ZX_OK, local.read(0, nullptr, 0, &outstanding_bytes));
  EXPECT_EQ(0u, outstanding_bytes);

  FX_VLOGS(1) << msg;
  output_compare_helper(std::move(local), -1, msg, nullptr, 0);
}

TEST(Logger, VLogWithTag) {
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0));
  const char* msg = "test message";
  const char* tags[] = {"tag"};
  FX_VLOGST(1, tags[0]) << msg;
  size_t outstanding_bytes = 10u;  // init to non zero value.
  ASSERT_EQ(ZX_OK, local.read(0, nullptr, 0, &outstanding_bytes));
  EXPECT_EQ(0u, outstanding_bytes);

  FX_LOG_SET_VERBOSITY(1);
  FX_VLOGST(2, tags[0]) << msg;
  outstanding_bytes = 10u;  // init to non zero value.
  ASSERT_EQ(ZX_OK, local.read(0, nullptr, 0, &outstanding_bytes));
  EXPECT_EQ(0u, outstanding_bytes);

  FX_VLOGST(1, tags[0]) << msg;
  output_compare_helper(std::move(local), -1, msg, tags, 1);
}

}  // namespace
