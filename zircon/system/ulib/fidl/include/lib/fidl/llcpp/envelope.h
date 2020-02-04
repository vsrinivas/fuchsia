// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ENVELOPE_H_
#define LIB_FIDL_LLCPP_ENVELOPE_H_

#include <zircon/fidl.h>

#include "tracking_ptr.h"

namespace fidl {

// Envelope is a typed version of fidl_envelope_t.
template <typename T>
struct Envelope {
  // The size of the entire envelope contents, including any additional
  // out-of-line objects that the envelope may contain. For example, a
  // vector<string>'s num_bytes for ["hello", "world"] would include the
  // string contents in the size, not just the outer vector. Always a multiple
  // of 8; must be zero if envelope is null.
  uint32_t num_bytes;

  // The number of handles in the envelope, including any additional
  // out-of-line objects that the envelope contains. Must be zero if envelope is null.
  uint32_t num_handles;

  // A pointer to the out-of-line envelope data.
  tracking_ptr<T> data;
};

static_assert(sizeof(Envelope<void>) == sizeof(fidl_envelope_t),
              "Envelope<T> must have the same size as fidl_envelope_t");
static_assert(offsetof(Envelope<void>, num_bytes) == offsetof(fidl_envelope_t, num_bytes),
              "num_bytes must have the same offset in Envelope<T> and fidl_envelope_t");
static_assert(sizeof(Envelope<void>().num_bytes) == sizeof(fidl_envelope_t().num_bytes),
              "num_bytes must have the same size in Envelope<T> and fidl_envelope_t");
static_assert(offsetof(Envelope<void>, num_handles) == offsetof(fidl_envelope_t, num_handles),
              "num_handles must have the same offset in Envelope<T> and fidl_envelope_t");
static_assert(sizeof(Envelope<void>().num_handles) == sizeof(fidl_envelope_t().num_handles),
              "num_handles must have the same size in Envelope<T> and fidl_envelope_t");
static_assert(offsetof(Envelope<void>, data) == offsetof(fidl_envelope_t, data),
              "data must have the same offset in Envelope<T> and fidl_envelope_t");
static_assert(sizeof(Envelope<void>().data) == sizeof(fidl_envelope_t().data),
              "data must have the same size in Envelope<T> and fidl_envelope_t");

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ENVELOPE_H_
