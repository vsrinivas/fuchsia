// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace/cwriter.h"

#include <zircon/compiler.h>

#include "garnet/lib/trace/internal/cevent_helpers.h"
#include "lib/fxl/logging.h"

__WEAK bool ctrace_is_enabled() {
  return false;
}

__WEAK bool ctrace_category_is_enabled(const char* category) {
  return false;
}

__WEAK ctrace_writer_t* ctrace_writer_acquire() {
  return nullptr;
}

__WEAK void ctrace_writer_release(ctrace_writer_t* writer) {
}

__WEAK void ctrace_register_current_thread(
    ctrace_writer_t* writer,
    ctrace_threadref_t* out_ref) {
  FXL_DCHECK(false);
  *out_ref = {};
}

__WEAK bool ctrace_register_category_string(
    ctrace_writer_t* writer,
    const char* string,
    bool check_category,
    ctrace_stringref_t* out_ref) {
  FXL_DCHECK(false);
  return false;
}

__WEAK void ctrace_register_string(
    ctrace_writer_t* writer,
    const char* string,
    ctrace_stringref_t* out_ref) {
  FXL_DCHECK(false);
  *out_ref = {};
}

// Stubs for the internal functions (used by the CTRACE_* macros).

__WEAK void ctrace_internal_write_instant_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    ctrace_scope_t scope,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_counter_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_duration_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_duration_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_async_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_async_instant_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_async_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_flow_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_flow_step_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_flow_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_internal_write_kernel_object_record(
    ctrace_writer_t* writer,
    zx_handle_t handle,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

// Stubs for the public facing API.

__WEAK void ctrace_write_instant_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    ctrace_scope_t scope,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_counter_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_duration_event_record(
    ctrace_writer_t* writer,
    uint64_t start_time,
    uint64_t end_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}
__WEAK void ctrace_write_duration_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_duration_end_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_async_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_async_instant_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_async_end_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_flow_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_flow_step_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_flow_end_event_record(
    ctrace_writer_t* writer,
    uint64_t ticks,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}

__WEAK void ctrace_write_kernel_object_record(
    ctrace_writer_t* writer,
    zx_handle_t handle,
    const ctrace_arglist_t* args) {
  FXL_DCHECK(false);
}
