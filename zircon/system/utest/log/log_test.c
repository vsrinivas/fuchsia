// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <zircon/syscalls.h>

#include <lib/log-writer-textfile/log-writer-textfile.h>
#include <lib/log/log.h>

#include <unittest/unittest.h>

const char* tmp_file_path = "/tmp/log_test_buffer";
const int file_size = 1024;

static FILE* open_tmp_file(void) {
    return fopen(tmp_file_path, "w+");
}

static void close_and_remove_tmp_file(FILE* f) {
    fclose(f);
    remove(tmp_file_path);
}

static void check_file_contents(FILE* f, char* expected) {
    char buf[file_size];
    memset(buf, 0, file_size);
    fseek(f, 0, SEEK_SET);
    fread(buf, file_size, 1, f);
    EXPECT_STR_EQ(buf, expected, "file doesn't match expected value");
}

static bool log_to_file_with_severity_test(void) {
    BEGIN_TEST;

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

    END_TEST;
}

static bool log_to_file_with_verbosity_test(void) {
    BEGIN_TEST;

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
                strncpy(expected, "[VERBOSITY:4 TAGS:[statictag, tag1, tag2, tag3]] message 3\n", file_size);
                break;
            case 4:
                LOGF(VERBOSE(5), "tag1", "tag2", "tag3", "tag4")("message %d", 4);
                strncpy(expected, "[VERBOSITY:5 TAGS:[statictag, tag1, tag2, tag3, tag4]] message 4\n", file_size);
                break;
        }
        check_file_contents(log_destination, expected);
        close_and_remove_tmp_file(log_destination);
        log_destroy_textfile_writer(log_writer);
    }

    END_TEST;
}

static bool set_min_level_test(void) {
    BEGIN_TEST;

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

    END_TEST;
}

static bool set_max_verbosity_test(void) {
    BEGIN_TEST;

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

    END_TEST;
}

static bool log_to_file_varying_numbers_of_static_tags_test(void) {
    BEGIN_TEST;

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
                }
                break;
            case 2: {
                    LOG_INITIALIZE(INFO, log_writer, "1", "2");
                    LOG(INFO, "a2", "b2")("test");
                    strncpy(expected, "[INFO TAGS:[1, 2, a2, b2]] test\n", file_size);
                }
                break;
            case 3: {
                    LOG_INITIALIZE(INFO, log_writer, "1", "2", "3");
                    LOG(INFO, "a3", "b3")("test");
                    strncpy(expected, "[INFO TAGS:[1, 2, 3, a3, b3]] test\n", file_size);
                }
                break;
            case 4: {
                    LOG_INITIALIZE(INFO, log_writer, "1", "2", "3", "4");
                    LOG(INFO, "a4", "b4")("test");
                    strncpy(expected, "[INFO TAGS:[1, 2, 3, 4, a4]] test\n", file_size);
                }
                break;
            case 5: {
                    LOG_INITIALIZE(INFO, log_writer, "1", "2", "3", "4", "5");
                    LOG(INFO, "a5", "b5")("test");
                    strncpy(expected, "[INFO TAGS:[1, 2, 3, 4, 5]] test\n", file_size);
                }
                break;
        }
        check_file_contents(log_destination, expected);
        close_and_remove_tmp_file(log_destination);
        log_destroy_textfile_writer(log_writer);
    }

    END_TEST;
}

BEGIN_TEST_CASE(log_tests)
RUN_TEST(log_to_file_with_severity_test);
RUN_TEST(log_to_file_with_verbosity_test);
RUN_TEST(set_min_level_test);
RUN_TEST(set_max_verbosity_test);
RUN_TEST(log_to_file_varying_numbers_of_static_tags_test);
END_TEST_CASE(log_tests)
