// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_INTL_LOOKUP_RUST_LOOKUP_H_
#define SRC_LIB_INTL_LOOKUP_RUST_LOOKUP_H_

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque returned type.
struct intl_lookup_t;

// The vtable for intl_lookup_t.  We only change from default values for tests,
// when we substitute a fake version of intl_lookup_t.  The vtable is explicit
// since we're using the C ABI.
typedef struct {
  intl_lookup_t* (*op_new)(size_t, char*[], int8_t*);
  void (*op_delete)(intl_lookup_t*);
  char* (*op_string)(intl_lookup_t*, uint64_t, int8_t*);
} intl_lookup_ops_t;

// Caller must take ownership of the returned pointer.
intl_lookup_t* intl_lookup_new(size_t len, char* locale_ids[], int8_t* status);

// Caller must give up ownership of the returned pointer.  The pointer is
// invalidated before return.
void intl_lookup_delete(intl_lookup_t* self);

// Looks up the string with supplied `message_id`, reporting the result into `status`.  The returned
// pointer is a valid UTF8-encoded C string, and is only valid if the `status` is OK.  The caller
// does not own the returned pointer and should not free it.
char* intl_lookup_string(intl_lookup_t* self, uint64_t message_id, int8_t* status);

// Fake implementations for tests only.

// A fake implementation of intl_lookup_new that (1) always returns an error on creation if the
// passed-in locale includes "en-US".  It also returns an error on intl_lookup_string's message
// ID being an odd number.  On an even number, it always returns "Hello world!".
intl_lookup_t* intl_lookup_new_fake_for_test(size_t len, char* locale_ids[], int8_t* status);
void intl_lookup_delete_fake_for_test(intl_lookup_t* self);
char* intl_lookup_string_fake_for_test(intl_lookup_t* self, uint64_t message_id, int8_t* status);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SRC_LIB_INTL_LOOKUP_RUST_LOOKUP_H_
