// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helpers for the C API event macros.

#pragma once

#include <assert.h>
#include <zircon/compiler.h>

#include "apps/tracing/lib/trace/cwriter.h"

__BEGIN_CDECLS

#define CTRACE_INTERNAL_ENABLED() ctrace_is_enabled()
#define CTRACE_INTERNAL_CATEGORY_ENABLED(category) \
  ctrace_category_is_enabled(category)

#define CTRACE_INTERNAL_NONCE() ctrace_internal_nonce()

#define CTRACE_INTERNAL_SCOPE_LABEL__(token) __ctrace_scope_##token
#define CTRACE_INTERNAL_SCOPE_LABEL_(token) CTRACE_INTERNAL_SCOPE_LABEL__(token)
#define CTRACE_INTERNAL_SCOPE_LABEL() CTRACE_INTERNAL_SCOPE_LABEL_(__COUNTER__)

#define CTRACE_INTERNAL_WRITER __ctrace_writer
#define CTRACE_INTERNAL_EVENT_CATEGORY_REF __ctrace_event_category_ref

#define CTRACE_INTERNAL_ARG_SPECS __ctrace_arg_specs
#define CTRACE_INTERNAL_ARGS __ctrace_args

#define CTRACE_INTERNAL_MAKE_LOCAL_ARGS(var, ...) \
  const ctrace_argspec_t var ## _specs[] = { __VA_ARGS__ }; \
  static_assert((sizeof(var ## _specs) / sizeof(var ## _specs[0])) <= CTRACE_MAX_ARGS, \
                "too many args"); \
  const ctrace_arglist_t var = { \
    sizeof(var ## _specs) / sizeof(var ## _specs[0]), \
    var ## _specs \
  } /* trailing ; provided by caller */

#define CTRACE_INTERNAL_MAKE_ARGS(...) \
  CTRACE_INTERNAL_MAKE_LOCAL_ARGS(CTRACE_INTERNAL_ARGS, ## __VA_ARGS__)

#define CTRACE_INTERNAL_EVENT_CATEGORY_REF_IS_EMPTY(catref) \
  ((catref)->encoded_value == 0 /*kEmpty*/)

#define CTRACE_INTERNAL_SIMPLE(stmt, args)                                  \
  do {                                                                      \
    ctrace_writer_t* CTRACE_INTERNAL_WRITER = ctrace_writer_acquire();      \
    if (CTRACE_INTERNAL_WRITER) {                                           \
      CTRACE_INTERNAL_MAKE_ARGS args;                                       \
      stmt;                                                                 \
      ctrace_writer_release(CTRACE_INTERNAL_WRITER);                        \
    }                                                                       \
  } while (0)

// TODO(jeffbrown): Determine whether we should try to do anything to
// further reduce the code expansion here.  The current goal is to avoid
// evaluating and expanding arguments unless the category is enabled.
#define CTRACE_INTERNAL_EVENT(category, stmt, args)                         \
  do {                                                                      \
    ctrace_writer_t* CTRACE_INTERNAL_WRITER = ctrace_writer_acquire();      \
    if (CTRACE_INTERNAL_WRITER) {                                           \
      ctrace_stringref_t CTRACE_INTERNAL_EVENT_CATEGORY_REF;                \
      if (ctrace_register_category_string(CTRACE_INTERNAL_WRITER,           \
                                          category, true,                   \
                                          &CTRACE_INTERNAL_EVENT_CATEGORY_REF)) { \
        CTRACE_INTERNAL_MAKE_ARGS args;                                     \
        stmt;                                                               \
      }                                                                     \
      ctrace_writer_release(CTRACE_INTERNAL_WRITER);                        \
    }                                                                       \
  } while (0)

#define CTRACE_INTERNAL_INSTANT(category, name, scope, args)                \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_instant_event_record(                 \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, scope, &CTRACE_INTERNAL_ARGS),                    \
      args)

#define CTRACE_INTERNAL_COUNTER(category, name, id, args)                   \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_counter_event_record(                 \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, &CTRACE_INTERNAL_ARGS),                       \
      args)

#define CTRACE_INTERNAL_DURATION_BEGIN(category, name, args)                \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_duration_begin_event_record(          \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, &CTRACE_INTERNAL_ARGS),                           \
      args)

#define CTRACE_INTERNAL_DURATION_END(category, name, args)                  \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_duration_end_event_record(            \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, &CTRACE_INTERNAL_ARGS),                           \
      args)

#define CTRACE_INTERNAL_DURATION_SCOPE(scope_label, scope_category, scope_name, \
                                       args)                                \
  CTRACE_INTERNAL_DURATION_SCOPE_VARIABLE(scope_label, scope_category, scope_name); \
  CTRACE_INTERNAL_DURATION_BEGIN(scope_label.category, scope_label.name,    \
                                args)

#define CTRACE_INTERNAL_DURATION(category, name, args)                         \
  CTRACE_INTERNAL_DURATION_SCOPE(CTRACE_INTERNAL_SCOPE_LABEL(), category, name, \
                                args)

#define CTRACE_INTERNAL_ASYNC_BEGIN(category, name, id, args)               \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_async_begin_event_record(             \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, &CTRACE_INTERNAL_ARGS),                       \
      args)

#define CTRACE_INTERNAL_ASYNC_INSTANT(category, name, id, args)             \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_async_instant_event_record(           \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, &CTRACE_INTERNAL_ARGS),                       \
      args)

#define CTRACE_INTERNAL_ASYNC_END(category, name, id, args)                 \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_async_end_event_record(               \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, &CTRACE_INTERNAL_ARGS),                       \
      args)

#define CTRACE_INTERNAL_FLOW_BEGIN(category, name, id, args)                \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_flow_begin_event_record(              \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, &CTRACE_INTERNAL_ARGS),                       \
      args)

#define CTRACE_INTERNAL_FLOW_STEP(category, name, id, args)                 \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_flow_step_event_record(               \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, &CTRACE_INTERNAL_ARGS),                       \
      args)

#define CTRACE_INTERNAL_FLOW_END(category, name, id, args)                  \
  CTRACE_INTERNAL_EVENT(                                                    \
      category, ctrace_internal_write_flow_end_event_record(                \
                    CTRACE_INTERNAL_WRITER, &CTRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, &CTRACE_INTERNAL_ARGS),                       \
      args)

#define CTRACE_INTERNAL_HANDLE(handle, args) \
  CTRACE_INTERNAL_SIMPLE(                    \
      ctrace_internal_write_kernel_object_record(CTRACE_INTERNAL_WRITER,    \
                                                 handle, &CTRACE_INTERNAL_ARGS), \
      args)

uint64_t ctrace_internal_nonce(void);

// When "destroyed" (by the cleanup attribute), writes a duration end event.
// Note: This implementation only acquires the |TraceWriter| at the end of
// the scope rather than holding it for the entire duration so we don't
// inadvertently delay trace shutdown if the code within the scope blocks
// for a long time.
typedef struct {
  const char* category;
  const char* name;
} ctrace_internal_duration_scope_t;

#define CTRACE_INTERNAL_DURATION_SCOPE_VARIABLE(variable, category, name) \
  __attribute__((cleanup(ctrace_internal_cleanup_duration_scope))) ctrace_internal_duration_scope_t variable; \
  ctrace_internal_make_duration_scope(&variable, (category), (name))

static inline void ctrace_internal_make_duration_scope(
    ctrace_internal_duration_scope_t* scope,
    const char* category, const char* name) {
  scope->category = category;
  scope->name = name;
}

void ctrace_internal_cleanup_duration_scope(ctrace_internal_duration_scope_t* scope);

// These ctrace_internal_write_* functions are for use by the above macros.
// Note: Their implementations could be put here (instead of being defined
// out-of-line), but are kept out for at least three reasons for now:
// debuggability, code size, and the macros themselves may get redone.

void ctrace_internal_write_instant_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    ctrace_scope_t scope,
    const ctrace_arglist_t* args);

void ctrace_internal_write_counter_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_internal_write_duration_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    const ctrace_arglist_t* args);

void ctrace_internal_write_duration_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    const ctrace_arglist_t* args);

void ctrace_internal_write_async_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_internal_write_async_instant_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_internal_write_async_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_internal_write_flow_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_internal_write_flow_step_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_internal_write_flow_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_internal_write_kernel_object_record(
    ctrace_writer_t* writer,
    zx_handle_t handle,
    const ctrace_arglist_t* args);

__END_CDECLS
