// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string.h>
#include <fbl/type_support.h>
#include <syslog/global.h>
#include <syslog/wire_format.h>
#include <unittest/unittest.h>
#include <zx/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

__BEGIN_CDECLS

// This does not come from header file as this function should only be used in
// tests and is not for general use.
void fx_log_reset_global(void);

__END_CDECLS

inline zx_status_t init_helper(zx_handle_t handle, const char** tags,
                               size_t ntags) {
  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = handle,
                               .tags = tags,
                               .num_tags = ntags};

  return fx_log_init_with_config(&config);
}

namespace {

class Cleanup {
 public:
  Cleanup() { fx_log_reset_global(); }
  ~Cleanup() { fx_log_reset_global(); }
};

}  // namespace

// Wrapper, so that we can pass bigger length param to EXPECT_STR_EQ for a
// proper match.
#define EXPECT_STRING_EQ(expected, actual, msg)                              \
  EXPECT_STR_EQ(                                                             \
      expected, actual,                                                      \
      strlen(expected) > strlen(actual) ? strlen(expected) : strlen(actual), \
      msg)

bool output_compare_helper(zx::socket local, fx_log_severity_t severity,
                           const char* msg, const char** tags, int num_tags) {
  fx_log_packet_t packet;
  ASSERT_EQ(ZX_OK, local.read(0, &packet, sizeof(packet), nullptr), "");
  EXPECT_EQ(severity, packet.metadata.severity, "");
  int pos = 0;
  for (int i = 0; i < num_tags; i++) {
    const char* tag = tags[i];
    auto tag_len = static_cast<int8_t>(strlen(tag));
    ASSERT_EQ(tag_len, packet.data[pos], "");
    pos++;
    ASSERT_STR_EQ(tag, packet.data + pos, tag_len, "");
    pos += tag_len;
  }
  ASSERT_EQ(0, packet.data[pos], "");
  pos++;
  EXPECT_STRING_EQ(msg, packet.data + pos, "");
  return true;
}

bool test_log_simple_write(void) {
  BEGIN_TEST;
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote), "");
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0), "");
  const char* msg = "test message";
  FX_LOG(INFO, nullptr, msg);
  output_compare_helper(fbl::move(local), FX_LOG_INFO, msg, nullptr, 0);
  END_TEST;
}

bool test_log_write(void) {
  BEGIN_TEST;
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote), "");
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0), "");
  FX_LOGF(INFO, nullptr, "%d, %s", 10, "just some number");
  output_compare_helper(fbl::move(local), FX_LOG_INFO, "10, just some number",
                        nullptr, 0);
  END_TEST;
}

bool test_log_severity(void) {
  BEGIN_TEST;
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote), "");
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0), "");

  FX_LOGF(DEBUG, nullptr, "%d, %s", 10, "just some number");
  size_t outstanding_bytes = 10u;  // init to non zero value.
  ASSERT_EQ(ZX_OK, local.read(0, nullptr, 0, &outstanding_bytes), "");
  EXPECT_EQ(0u, outstanding_bytes);

  FX_LOGF(WARNING, nullptr, "%d, %s", 10, "just some number");
  output_compare_helper(fbl::move(local), FX_LOG_WARNING,
                        "10, just some number", nullptr, 0);
  END_TEST;
}

bool test_log_write_with_tag(void) {
  BEGIN_TEST;
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote), "");
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), nullptr, 0), "");
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  const char* tags[] = {"tag"};
  output_compare_helper(fbl::move(local), FX_LOG_INFO, "10, just some string",
                        tags, 1);
  END_TEST;
}

bool test_log_write_with_global_tag(void) {
  BEGIN_TEST;
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote), "");
  const char* gtags[] = {"gtag"};
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), gtags, 1), "");
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  const char* tags[] = {"gtag", "tag"};
  output_compare_helper(fbl::move(local), FX_LOG_INFO, "10, just some string",
                        tags, 2);
  END_TEST;
}

bool test_log_write_with_multi_global_tag(void) {
  BEGIN_TEST;
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote), "");
  const char* gtags[] = {"gtag", "gtag2"};
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), gtags, 2), "");
  FX_LOGF(INFO, "tag", "%d, %s", 10, "just some string");
  const char* tags[] = {"gtag", "gtag2", "tag"};
  output_compare_helper(fbl::move(local), FX_LOG_INFO, "10, just some string",
                        tags, 3);
  END_TEST;
}

bool test_msg_length_limit(void) {
  BEGIN_TEST;
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote), "");
  const char* gtags[] = {"gtag", "gtag2"};
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), gtags, 2), "");
  char msg[2048] = {0};
  memset(msg, 'a', sizeof(msg) - 1);
  FX_LOGF(INFO, "tag", "%s", msg);
  fx_log_packet_t packet;
  int msg_size = sizeof(packet.data) - 4 - 12;
  char expected[msg_size];
  memset(expected, 'a', msg_size - 4);
  memset(expected + msg_size - 4, '.', 3);
  expected[msg_size - 1] = 0;
  const char* tags[] = {"gtag", "gtag2", "tag"};
  output_compare_helper(fbl::move(local), FX_LOG_INFO, expected, tags, 3);
  END_TEST;
}

bool test_tag_length_limit(void) {
  BEGIN_TEST;
  Cleanup cleanup;
  zx::socket local, remote;
  EXPECT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote), "");
  char gtags_buffer[FX_LOG_MAX_TAGS][FX_LOG_MAX_TAG_LEN + 1];
  memset(gtags_buffer, 't', sizeof(gtags_buffer));
  const char* gtags[FX_LOG_MAX_TAGS];
  for (int i = 0; i < FX_LOG_MAX_TAGS; i++) {
    gtags_buffer[i][0] = static_cast<char>(49 + i);  // '1' + i
    gtags_buffer[i][FX_LOG_MAX_TAG_LEN] = 0;
    gtags[i] = gtags_buffer[i];
  }
  ASSERT_EQ(ZX_OK, init_helper(remote.release(), gtags, FX_LOG_MAX_TAGS), "");

  char tag[FX_LOG_MAX_TAG_LEN + 1];
  memcpy(tag, gtags[FX_LOG_MAX_TAGS - 1], sizeof(tag));
  tag[0]++;
  char msg[] = "some text";
  FX_LOGF(INFO, tag, "%s", msg);
  const char* tags[FX_LOG_MAX_TAGS + 1];
  for (int i = 0; i < FX_LOG_MAX_TAGS; i++) {
    gtags_buffer[i][FX_LOG_MAX_TAG_LEN - 1] = 0;
    tags[i] = gtags_buffer[i];
  }
  tag[FX_LOG_MAX_TAG_LEN - 1] = 0;
  tags[FX_LOG_MAX_TAGS] = tag;
  output_compare_helper(fbl::move(local), FX_LOG_INFO, msg, tags,
                        FX_LOG_MAX_TAGS + 1);
  END_TEST;
}

BEGIN_TEST_CASE(syslog_socket_tests)
RUN_TEST(test_log_simple_write)
RUN_TEST(test_log_write)
RUN_TEST(test_log_severity)
RUN_TEST(test_log_write_with_tag)
RUN_TEST(test_log_write_with_global_tag)
RUN_TEST(test_log_write_with_multi_global_tag)
END_TEST_CASE(syslog_socket_tests)

BEGIN_TEST_CASE(syslog_socket_tests_edge_cases)
RUN_TEST(test_msg_length_limit)
RUN_TEST(test_tag_length_limit)
END_TEST_CASE(syslog_socket_tests_edge_cases)
