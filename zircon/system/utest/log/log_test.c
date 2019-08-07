// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/log-writer-logger/log-writer-logger.h>
#include <lib/log-writer-logger/wire_format.h>
#include <lib/log-writer-textfile/log-writer-textfile.h>
#include <lib/log/log.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

const char* tmp_file_path = "/tmp/log_test_buffer";
const int file_size = 1024;

static FILE* open_tmp_file(void) { return fopen(tmp_file_path, "w+"); }

static void close_and_remove_tmp_file(FILE* f) {
  fclose(f);
  remove(tmp_file_path);
}

static void check_file_contents(FILE* f, char* expected) {
  char buf[file_size];
  memset(buf, 0, file_size);
  fseek(f, 0, SEEK_SET);
  fread(buf, file_size, 1, f);
  EXPECT_STR_EQ((const char*)(buf), expected, "file doesn't match expected value");
}

TEST(LogTestCase, LogToFileWithSeverity) {
  for (int i = 0; i < 5; i++) {
    FILE* log_destination = open_tmp_file();
    ASSERT_NE(log_destination, NULL, "open_tmp_file failed");

    log_writer_t* log_writer = log_create_textfile_writer(log_destination);
    LOG_INITIALIZE(INFO, log_writer, "statictag");

    char expected[file_size];
    memset(expected, 0, file_size);

    switch (i) {
      case 0:
        LOGF(INFO)("message %d", 0);
        strncpy(expected, "[INFO TAGS:[statictag]] message 0\n", file_size);
        break;
      case 1:
        LOGF(WARNING, "tag1")("message %d", 1);
        strncpy(expected, "[WARNING TAGS:[statictag, tag1]] message 1\n", file_size);
        break;
      case 2:
        LOGF(ERROR, "tag1", "tag2")("message %d", 2);
        strncpy(expected, "[ERROR TAGS:[statictag, tag1, tag2]] message 2\n", file_size);
        break;
      case 3:
        LOGF(FATAL, "tag1", "tag2", "tag3")("message %d", 3);
        strncpy(expected, "[FATAL TAGS:[statictag, tag1, tag2, tag3]] message 3\n", file_size);
        break;
      case 4:
        LOGF(INFO, "tag1", "tag2", "tag3", "tag4")("message %d", 3);
        strncpy(expected, "[INFO TAGS:[statictag, tag1, tag2, tag3, tag4]] message 3\n", file_size);
        break;
    }
    check_file_contents(log_destination, expected);
    close_and_remove_tmp_file(log_destination);
    log_destroy_textfile_writer(log_writer);
  }
}

TEST(LogTestCase, LogToFileWithVerbosity) {
  for (int i = 0; i < 5; i++) {
    FILE* log_destination = open_tmp_file();
    ASSERT_NE(log_destination, NULL, "open_tmp_file failed");

    log_writer_t* log_writer = log_create_textfile_writer(log_destination);
    LOG_INITIALIZE(VERBOSE(10), log_writer, "statictag");

    char expected[file_size];
    memset(expected, 0, file_size);

    switch (i) {
      case 0:
        LOGF(VERBOSE(1))("message %d", i);
        strncpy(expected, "[VERBOSITY:1 TAGS:[statictag]] message 0\n", file_size);
        break;
      case 1:
        LOGF(VERBOSE(2), "tag1")("message %d", 1);
        strncpy(expected, "[VERBOSITY:2 TAGS:[statictag, tag1]] message 1\n", file_size);
        break;
      case 2:
        LOGF(VERBOSE(3), "tag1", "tag2")("message %d", 2);
        strncpy(expected, "[VERBOSITY:3 TAGS:[statictag, tag1, tag2]] message 2\n", file_size);
        break;
      case 3:
        LOGF(VERBOSE(4), "tag1", "tag2", "tag3")("message %d", 3);
        strncpy(expected, "[VERBOSITY:4 TAGS:[statictag, tag1, tag2, tag3]] message 3\n",
                file_size);
        break;
      case 4:
        LOGF(VERBOSE(5), "tag1", "tag2", "tag3", "tag4")("message %d", 4);
        strncpy(expected, "[VERBOSITY:5 TAGS:[statictag, tag1, tag2, tag3, tag4]] message 4\n",
                file_size);
        break;
    }
    check_file_contents(log_destination, expected);
    close_and_remove_tmp_file(log_destination);
    log_destroy_textfile_writer(log_writer);
  }
}

TEST(LogTestCase, SetMinLevel) {
  FILE* log_destination = open_tmp_file();
  ASSERT_NE(log_destination, NULL, "open_tmp_file failed");
  log_writer_t* log_writer = log_create_textfile_writer(log_destination);
  LOG_INITIALIZE(ERROR, log_writer, "tag");

  char expected[file_size];
  memset(expected, 0, file_size);

  LOG(INFO)("test");
  LOG(FATAL)("test");
  LOG(WARNING)("test");
  LOG(ERROR)("test");

  strncpy(expected, "[FATAL TAGS:[tag]] test\n[ERROR TAGS:[tag]] test\n", file_size);

  check_file_contents(log_destination, expected);
  close_and_remove_tmp_file(log_destination);
  log_destroy_textfile_writer(log_writer);
}

TEST(LogTestCase, SetMaxVerbosity) {
  FILE* log_destination = open_tmp_file();
  ASSERT_NE(log_destination, NULL, "open_tmp_file failed");
  log_writer_t* log_writer = log_create_textfile_writer(log_destination);
  LOG_INITIALIZE(VERBOSE(5), log_writer, "tag");

  char expected[file_size];
  memset(expected, 0, file_size);

  LOGF(VERBOSE(10))("te%s", "st");
  LOGF(VERBOSE(2))("te%s", "st");
  LOGF(VERBOSE(8))("te%s", "st");
  LOGF(VERBOSE(5))("te%s", "st");

  strncpy(expected, "[VERBOSITY:2 TAGS:[tag]] test\n[VERBOSITY:5 TAGS:[tag]] test\n", file_size);

  check_file_contents(log_destination, expected);
  close_and_remove_tmp_file(log_destination);
  log_destroy_textfile_writer(log_writer);
}

TEST(LogTestCase, LogToFileVaryingNumbersOfStaticTags) {
  for (int i = 0; i < 6; i++) {
    FILE* log_destination = open_tmp_file();
    ASSERT_NE(log_destination, NULL, "open_tmp_file failed");

    log_writer_t* log_writer = log_create_textfile_writer(log_destination);

    char expected[file_size];
    memset(expected, 0, file_size);

    switch (i) {
      case 0:
        LOG_INITIALIZE(INFO, log_writer);
        LOG(INFO, "a0", "b0")("test");
        strncpy(expected, "[INFO TAGS:[a0, b0]] test\n", file_size);
        break;
      case 1: {
        LOG_INITIALIZE(INFO, log_writer, "1");
        LOG(INFO, "a1", "b1")("test");
        strncpy(expected, "[INFO TAGS:[1, a1, b1]] test\n", file_size);
      } break;
      case 2: {
        LOG_INITIALIZE(INFO, log_writer, "1", "2");
        LOG(INFO, "a2", "b2")("test");
        strncpy(expected, "[INFO TAGS:[1, 2, a2, b2]] test\n", file_size);
      } break;
      case 3: {
        LOG_INITIALIZE(INFO, log_writer, "1", "2", "3");
        LOG(INFO, "a3", "b3")("test");
        strncpy(expected, "[INFO TAGS:[1, 2, 3, a3, b3]] test\n", file_size);
      } break;
      case 4: {
        LOG_INITIALIZE(INFO, log_writer, "1", "2", "3", "4");
        LOG(INFO, "a4", "b4")("test");
        strncpy(expected, "[INFO TAGS:[1, 2, 3, 4, a4]] test\n", file_size);
      } break;
      case 5: {
        LOG_INITIALIZE(INFO, log_writer, "1", "2", "3", "4", "5");
        LOG(INFO, "a5", "b5")("test");
        strncpy(expected, "[INFO TAGS:[1, 2, 3, 4, 5]] test\n", file_size);
      } break;
    }
    check_file_contents(log_destination, expected);
    close_and_remove_tmp_file(log_destination);
    log_destroy_textfile_writer(log_writer);
  }
}

TEST(LogTestCase, LogToLoggerWithSeverity) {
  for (int i = 0; i < 5; i++) {
    log_writer_t* log_writer = log_create_logger_writer();

    zx_handle_t writer_socket, server_socket;
    EXPECT_EQ(ZX_OK, zx_socket_create(ZX_SOCKET_DATAGRAM, &writer_socket, &server_socket),
              "failed to create socket");
    log_set_logger_writer_socket(log_writer, writer_socket);

    LOG_INITIALIZE(INFO, log_writer, "statictag");

    log_level_t expected_level;
    char expected_msg[file_size];
    memset(expected_msg, 0, file_size);
    const char* expected_tags[6] = {NULL, NULL, NULL, NULL, NULL, NULL};

    switch (i) {
      case 0:
        expected_level = LOG_LEVEL_INFO;
        LOGF(INFO)("test %s", "message");
        strncpy(expected_msg, "test message", file_size);
        expected_tags[0] = "statictag";
        break;
      case 1:
        expected_level = LOG_LEVEL_WARNING;
        LOGF(WARNING, "tag1")("test %s", "message");
        strncpy(expected_msg, "test message", file_size);
        expected_tags[0] = "statictag";
        expected_tags[1] = "tag1";
        break;
      case 2:
        expected_level = LOG_LEVEL_ERROR;
        LOGF(ERROR, "tag1", "tag2")("test %s", "message");
        strncpy(expected_msg, "test message", file_size);
        expected_tags[0] = "statictag";
        expected_tags[1] = "tag1";
        expected_tags[2] = "tag2";
        break;
      case 3:
        expected_level = LOG_LEVEL_FATAL;
        LOGF(FATAL, "tag1", "tag2", "tag3")("test %s", "message");
        strncpy(expected_msg, "test message", file_size);
        expected_tags[0] = "statictag";
        expected_tags[1] = "tag1";
        expected_tags[2] = "tag2";
        expected_tags[3] = "tag3";
        break;
      case 4:
        expected_level = LOG_LEVEL_INFO;
        LOGF(INFO, "tag1", "tag2", "tag3", "tag4")("test %s", "message");
        strncpy(expected_msg, "test message", file_size);
        expected_tags[0] = "statictag";
        expected_tags[1] = "tag1";
        expected_tags[2] = "tag2";
        expected_tags[3] = "tag3";
        expected_tags[4] = "tag4";
        break;
      default:
        ASSERT_EQ(0, 1, "impossible");
    }

    uint8_t buf[LOG_MAX_DATAGRAM_LEN];
    size_t actual;
    ASSERT_EQ(
        ZX_OK,
        zx_object_wait_one(server_socket, ZX_SOCKET_READABLE,
                           zx_time_add_duration(zx_clock_get_monotonic(), zx_duration_from_sec(1)),
                           NULL),  // wait for 1s
        "no message was written to the socket");
    ASSERT_EQ(ZX_OK, zx_socket_read(server_socket, 0, buf, LOG_MAX_DATAGRAM_LEN, &actual),
              "failed to read from socket");
    EXPECT_OK(zx_handle_close(server_socket), "failed to close socket");

    log_packet_t* packet = (log_packet_t*)buf;
    EXPECT_EQ((uint32_t)0, packet->metadata.dropped_logs, "unexpected dropped logs");
    EXPECT_EQ(expected_level, packet->metadata.level, "unexpected level");

    char* data_ptr = packet->data;
    const char** tag_iterator = expected_tags;
    while (*tag_iterator != NULL) {
      uint8_t tag_size = *data_ptr;
      EXPECT_NE(0, tag_size, "reached the end of tags too soon");
      data_ptr++;

      char tag_buf[LOG_MAX_DATAGRAM_LEN];
      memset(tag_buf, 0, LOG_MAX_DATAGRAM_LEN);

      memcpy(tag_buf, data_ptr, tag_size);
      data_ptr += tag_size;

      EXPECT_STR_EQ(*tag_iterator, tag_buf, "tag in message doesn't match expected value");

      tag_iterator++;
    }
    EXPECT_EQ(0, *data_ptr, "more tags than expected");
    data_ptr++;
    EXPECT_STR_EQ((const char*)expected_msg, data_ptr,
                  "received message doesn't match expected value");

    log_destroy_logger_writer(log_writer);
  }
}
