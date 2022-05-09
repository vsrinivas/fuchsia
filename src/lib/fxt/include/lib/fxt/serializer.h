// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Given a Writer implementing the Writer CRTP in writer.h, provide an api
// over the writer to allow serializing fxt to the Writer.
//
// Based heavily on libTrace in zircon/system/ulib/trace to allow compatibility,
// but modified to enable passing in an arbitrary buffering system.

#ifndef SRC_LIB_FXT_INCLUDE_LIB_FXT_SERIALIZER_H_
#define SRC_LIB_FXT_INCLUDE_LIB_FXT_SERIALIZER_H_

#include <lib/zx/status.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "fields.h"
#include "record_types.h"
#include "writer_internal.h"

namespace fxt {
inline uint64_t MakeHeader(RecordType type, size_t size_bytes) {
  return RecordFields::Type::Make(ToUnderlyingType(type)) |
         RecordFields::RecordSize::Make(BytesToWords(size_bytes));
}

template <typename Writer, internal::EnableIfWriter<Writer> = 0>
zx_status_t WriteInitializationRecord(Writer* writer, zx_ticks_t ticks_per_second) {
  const size_t record_size = 16;
  uint64_t header = MakeHeader(RecordType::kInitialization, record_size);
  zx::status<typename Writer::Reservation> res = writer->Reserve(header);
  if (res.is_ok()) {
    res->WriteWord(ticks_per_second);
    res->Commit();
  }
  return res.status_value();
}
}  // namespace fxt

#endif  // SRC_LIB_FXT_INCLUDE_LIB_FXT_SERIALIZER_H_
