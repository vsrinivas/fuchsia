// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/tests/cwriter_unittest.h"

#include <zircon/syscalls.h>

#include <stdlib.h>
#include <threads.h>

#include "apps/tracing/lib/trace/cevent.h"
#include "apps/tracing/lib/trace/cwriter.h"
#include "apps/tracing/lib/trace/tests/ctrace_test_harness.h"

#define NEW_ARGUMENT_LIST(var) \
  CTRACE_MAKE_LOCAL_ARGS(var, TA_I32("int32", 42), \
    TA_I64("int64", -42), \
    TA_DOUBLE("double", 42.42), \
    TA_STR("cstring", "constant"), \
    TA_PTR("pointer", "pointer-value"), \
    TA_KOID("koid", (zx_koid_t)(1 << 10)))

bool cwriter_test_string_registration_and_retrieval(void) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  C_EXPECT_NE(writer, NULL);

  const char* t1 = "test";
  const char* t2 = "test";
  const char* t3 = "different";
  ctrace_stringref_t ref1, ref1b;
  ctrace_stringref_t ref2, ref2b;
  ctrace_stringref_t ref3, ref3b;
  ctrace_register_string(writer, t1, &ref1);
  ctrace_register_string(writer, t2, &ref2);
  ctrace_register_string(writer, t3, &ref3);
  C_EXPECT_TRUE(ref1.encoded_value == ref2.encoded_value);
  C_EXPECT_TRUE(ref1.encoded_value != ref3.encoded_value);
  ctrace_register_string(writer, t1, &ref1b);
  ctrace_register_string(writer, t2, &ref2b);
  ctrace_register_string(writer, t3, &ref3b);
  C_EXPECT_EQ(ref1.encoded_value, ref1b.encoded_value);
  C_EXPECT_EQ(ref2.encoded_value, ref2b.encoded_value);
  C_EXPECT_EQ(ref3.encoded_value, ref3b.encoded_value);

  ctrace_writer_release(writer);
  return true;
}

bool cwriter_test_bulk_string_registration_and_retrieval(void) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  struct {
    char* string;
    ctrace_stringref_t ref;
  } * ids;
  const int test_count = 4096;

  ids = calloc(test_count, sizeof(*ids));
  C_EXPECT_NE(ids, NULL);

  for (int i = 0; i < test_count; i++) {
    ids[i].string = malloc(10);
    C_EXPECT_NE(ids[i].string, NULL);
    sprintf(ids[i].string, "%d", i);
    ctrace_register_string(writer, ids[i].string, &ids[i].ref);
    C_EXPECT_NE(0u, ids[i].ref.encoded_value);
  }

  for (int i = 0; i < test_count; i++) {
    ctrace_stringref_t ref2;
    ctrace_register_string(writer, ids[i].string, &ref2);
    C_EXPECT_EQ(ids[i].ref.encoded_value, ref2.encoded_value);
    free(ids[i].string);
  }
  free(ids);

  ctrace_writer_release(writer);
  return true;
}

bool cwriter_test_thread_registration(void) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  ctrace_threadref_t ref;

  ctrace_register_current_thread(writer, &ref);
  C_EXPECT_NE(0u, ref.encoded_value);

  ctrace_writer_release(writer);
  return true;
}

static int BulkThreadRegistration_ThreadFunc(void* arg) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  ctrace_threadref_t ref;

  ctrace_register_current_thread(writer, &ref);
  C_EXPECT_NE(0u, ref.encoded_value);

  ctrace_writer_release(writer);
  return 0;
}

bool cwriter_test_bulk_thread_registration(void) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  ctrace_threadref_t ref;

  ctrace_register_current_thread(writer, &ref);
  C_EXPECT_NE(0u, ref.encoded_value);

  thrd_t threads[10];

  for (size_t i = 0; i < 10; i++) {
    C_EXPECT_EQ(thrd_create(&threads[i], BulkThreadRegistration_ThreadFunc, NULL), thrd_success);
  }

  for (size_t i = 0; i < 10; i++) {
    C_EXPECT_EQ(thrd_join(threads[i], NULL), thrd_success);
  }

  ctrace_writer_release(writer);
  return true;
}

bool cwriter_test_event_writing(void) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  ctrace_threadref_t thread_ref;
  ctrace_stringref_t category_ref;
  ctrace_stringref_t name_ref;

  ctrace_register_current_thread(writer, &thread_ref);
  C_EXPECT_TRUE(ctrace_register_category_string(writer, "cat", true, &category_ref));
  ctrace_register_string(writer, "name", &name_ref);

  ctrace_write_duration_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                       &category_ref, &name_ref, NULL);
  ctrace_write_duration_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                     &category_ref, &name_ref, NULL);
  ctrace_write_async_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                    &category_ref, &name_ref, 42, NULL);
  ctrace_write_async_instant_event_record(writer, zx_ticks_get(), &thread_ref,
                                      &category_ref, &name_ref, 42, NULL);
  ctrace_write_async_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                  &category_ref, &name_ref, 42, NULL);

  ctrace_write_flow_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                   &category_ref, &name_ref, 42, NULL);
  ctrace_write_flow_step_event_record(writer, zx_ticks_get(), &thread_ref,
                                  &category_ref, &name_ref, 42, NULL);
  ctrace_write_flow_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                 &category_ref, &name_ref, 42, NULL);

  ctrace_writer_release(writer);
  return true;
}

static int EventWritingMultiThreaded_ThreadFunc(void* arg) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  ctrace_threadref_t thread_ref;
  ctrace_stringref_t category_ref;
  ctrace_stringref_t name_ref;

  ctrace_register_current_thread(writer, &thread_ref);
  C_EXPECT_TRUE(ctrace_register_category_string(writer, "cat", true, &category_ref));
  ctrace_register_string(writer, "name", &name_ref);

  ctrace_write_duration_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                       &category_ref, &name_ref, NULL);
  ctrace_write_duration_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                     &category_ref, &name_ref, NULL);
  ctrace_write_async_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                    &category_ref, &name_ref, 42, NULL);
  ctrace_write_async_instant_event_record(writer, zx_ticks_get(), &thread_ref,
                                      &category_ref, &name_ref, 42, NULL);
  ctrace_write_async_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                  &category_ref, &name_ref, 42, NULL);
  ctrace_write_flow_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                   &category_ref, &name_ref, 42, NULL);
  ctrace_write_flow_step_event_record(writer, zx_ticks_get(), &thread_ref,
                                  &category_ref, &name_ref, 42, NULL);
  ctrace_write_flow_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                 &category_ref, &name_ref, 42, NULL);

  ctrace_writer_release(writer);
  return 0;
}

bool cwriter_test_event_writing_multithreaded(void) {
  thrd_t threads[10];

  for (size_t i = 0; i < 10; i++) {
    C_EXPECT_EQ(thrd_create(&threads[i], EventWritingMultiThreaded_ThreadFunc, NULL), thrd_success);
  }

  for (size_t i = 0; i < 10; i++) {
    C_EXPECT_EQ(thrd_join(threads[i], NULL), thrd_success);
  }

  return true;
}

bool cwriter_test_event_writing_with_arguments(void) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  ctrace_threadref_t thread_ref;
  ctrace_stringref_t category_ref;
  ctrace_stringref_t name_ref;

  ctrace_register_current_thread(writer, &thread_ref);
  C_EXPECT_TRUE(ctrace_register_category_string(writer, "cat", true, &category_ref));
  ctrace_register_string(writer, "name", &name_ref);

  NEW_ARGUMENT_LIST(args);

  ctrace_write_duration_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                       &category_ref, &name_ref, &args);

  ctrace_write_duration_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                     &category_ref, &name_ref, &args);

  ctrace_write_async_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                    &category_ref, &name_ref, 42, &args);

  ctrace_write_async_instant_event_record(writer, zx_ticks_get(), &thread_ref,
                                      &category_ref, &name_ref, 42, &args);

  ctrace_write_async_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                  &category_ref, &name_ref, 42, &args);

  ctrace_write_flow_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                   &category_ref, &name_ref, 42, &args);

  ctrace_write_flow_step_event_record(writer, zx_ticks_get(), &thread_ref,
                                  &category_ref, &name_ref, 42, &args);

  ctrace_write_flow_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                 &category_ref, &name_ref, 42, &args);

  ctrace_writer_release(writer);
  return true;
}

static int EventWritingWithArgumentsMultiThreaded_ThreadFunc(void* arg) {
  ctrace_writer_t* writer = ctrace_writer_acquire();
  ctrace_threadref_t thread_ref;
  ctrace_stringref_t category_ref;
  ctrace_stringref_t name_ref;

  ctrace_register_current_thread(writer, &thread_ref);
  C_EXPECT_TRUE(ctrace_register_category_string(writer, "cat", true, &category_ref));
  ctrace_register_string(writer, "name", &name_ref);

  NEW_ARGUMENT_LIST(args);

  ctrace_write_duration_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                       &category_ref, &name_ref, &args);

  ctrace_write_duration_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                     &category_ref, &name_ref, &args);

  ctrace_write_async_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                    &category_ref, &name_ref, 42, &args);

  ctrace_write_async_instant_event_record(writer, zx_ticks_get(), &thread_ref,
                                      &category_ref, &name_ref, 42, &args);

  ctrace_write_async_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                  &category_ref, &name_ref, 42, &args);

  ctrace_write_flow_begin_event_record(writer, zx_ticks_get(), &thread_ref,
                                   &category_ref, &name_ref, 42, &args);

  ctrace_write_flow_step_event_record(writer, zx_ticks_get(), &thread_ref,
                                  &category_ref, &name_ref, 42, &args);

  ctrace_write_flow_end_event_record(writer, zx_ticks_get(), &thread_ref,
                                 &category_ref, &name_ref, 42, &args);

  ctrace_writer_release(writer);
  return 0;
}

bool cwriter_test_event_writing_with_arguments_multithreaded(void) {
  thrd_t threads[10];

  for (size_t i = 0; i < 10; i++) {
    C_EXPECT_EQ(thrd_create(&threads[i], EventWritingWithArgumentsMultiThreaded_ThreadFunc, NULL), thrd_success);
  }

  for (size_t i = 0; i < 10; i++) {
    C_EXPECT_EQ(thrd_join(threads[i], NULL), thrd_success);
  }

  return true;
}
