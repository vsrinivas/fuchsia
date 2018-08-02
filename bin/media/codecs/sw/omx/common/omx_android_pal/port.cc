// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "log/log.h"

#include <lib/fxl/debug/debugger.h>
#include <lib/fxl/logging.h>

extern "C" {

// NOP replacement for Android's property_get:
int property_get(const char* key, char* value, const char* default_value) {
  return 0;
}

int __android_log_print(int priority, const char* tag, const char* format,
                        ...) {
  if (priority == ANDROID_LOG_VERBOSE) {
    return 1;
  }
  // TODO: maybe concatenate before sending to printf in hopes that less output
  // would be split (if it starts to become a problem).
  const char* local_tag = tag;
  if (!local_tag) {
    local_tag = "<NO_TAG>";
  }
  printf("%d %s ", priority, local_tag);
  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  va_end(ap);
  printf("\n");
  return 1;
}

void __android_log_assert(const char* condition, const char* tag,
                          const char* format, ...) {
  // TODO: maybe concatenate before sending to printf in hopes that less output
  // would be split (if it starts to become a problem).
  const char* local_tag = tag;
  if (!local_tag) {
    local_tag = "<NO_TAG>";
  }
  printf("__android_log_assert: condition: %s tag: %s ", condition, local_tag);
  if (format) {
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
  }
  printf("\n");

  fxl::BreakDebugger();
  exit(-1);
}

void __assert2(const char* file, int line, const char* function,
               const char* failed_expression) {
  printf(
      "omx_android_pal assert failed: file: %s line: %d function: %s "
      "failed_expression: %s",
      file, line, function, failed_expression);
  assert(false && "see omx_android_pal assert failure output above");
}

int __android_log_error_write(int tag, const char* sub_tag, int32_t uid,
                              const char* data, uint32_t data_length) {
  // For now we drop the data part - if we see any of this happening we may need
  // to plumb that part.
  printf(
      "__android_log_error_write: tag: %d sub_tag: %s uid: %d data_length: "
      "%d\n",
      tag, sub_tag, uid, data_length);
  return 0;
}

}  // extern "C"

namespace android {

struct AString;

void hexdump(const void* data, size_t size, size_t indent, AString* append_to) {
  printf("hexdump() requested but not yet implemented\n");
}

}  // namespace android
