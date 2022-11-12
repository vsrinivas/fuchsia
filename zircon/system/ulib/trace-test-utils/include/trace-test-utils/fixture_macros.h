// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_TRACE_FIXTURE_MACROS_H_
#define ZIRCON_SYSTEM_UTEST_TRACE_FIXTURE_MACROS_H_

#include <trace-test-utils/fixture.h>
#include <zxtest/zxtest.h>

#define DEFAULT_BUFFER_SIZE_BYTES (1024u * 1024u)

// This isn't a do-while because of the cleanup.
#define BEGIN_TRACE_TEST_ETC(attach_to_thread, mode, buffer_size)   \
  __attribute__((cleanup(fixture_scope_cleanup))) bool __scope = 0; \
  (void)__scope;                                                    \
  fixture_set_up((attach_to_thread), (mode), (buffer_size))

#define BEGIN_TRACE_TEST \
  BEGIN_TRACE_TEST_ETC(kNoAttachToThread, TRACE_BUFFERING_MODE_ONESHOT, DEFAULT_BUFFER_SIZE_BYTES)

#define END_TRACE_TEST

#ifndef NTRACE

#ifdef __cplusplus
#define BEGIN_TRACE_TEST_WITH_CATEGORIES_ETC(attach_to_thread, mode, buffer_size, categories) \
  __attribute__((cleanup(fixture_scope_cleanup))) bool __scope = 0;                           \
  (void)__scope;                                                                              \
  fixture_set_up_with_categories((attach_to_thread), (mode), (buffer_size), (categories))
#define BEGIN_TRACE_TEST_WITH_CATEGORIES(categories)                                    \
  BEGIN_TRACE_TEST_WITH_CATEGORIES_ETC(kNoAttachToThread, TRACE_BUFFERING_MODE_ONESHOT, \
                                       DEFAULT_BUFFER_SIZE_BYTES, (categories))

#define ASSERT_RECORDS(expected_c, expected_cpp) \
  ASSERT_TRUE(fixture_compare_records(expected_c expected_cpp), "record mismatch")
#define ASSERT_N_RECORDS(max_num_recs, expected_c, expected_cpp, records, skip_count)              \
  ASSERT_TRUE(                                                                                     \
      fixture_compare_n_records((max_num_recs), expected_c expected_cpp, (records), (skip_count)), \
      "record mismatch")
#else
#define ASSERT_RECORDS(expected_c, expected_cpp) \
  ASSERT_TRUE(fixture_compare_records(expected_c), "record mismatch")
#endif  // __cplusplus

#else  // NTRACE

#define ASSERT_RECORDS(expected_c, expected_cpp) \
  ASSERT_TRUE(fixture_compare_records(""), "record mismatch")
#ifdef __cplusplus
#define ASSERT_N_RECORDS(max_num_recs, expected_c, expected_cpp, records, skip_count) \
  ASSERT_TRUE(fixture_compare_records((max_num_recs), "", (records), (skip_count)),   \
              "record mismatch")
#endif

#endif  // NTRACE

#endif  // ZIRCON_SYSTEM_UTEST_TRACE_FIXTURE_MACROS_H_
