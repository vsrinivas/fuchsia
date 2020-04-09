// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DIAGNOSTICS_LIB_STREAMS_ENCODE_H_
#define SRC_DIAGNOSTICS_LIB_STREAMS_ENCODE_H_

#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <zircon/status.h>

#include <vector>

namespace streams {
// log_record: writes a given record in trace format to the buffer
zx_status_t log_record(const fuchsia::diagnostics::stream::Record&, std::vector<uint8_t>*);

namespace internal {

void write_string(std::string& str, std::vector<uint8_t>*);
void write_signed_int(int, std::vector<uint8_t>*);
void write_unsigned_int(unsigned int, std::vector<uint8_t>*);
void write_float(float, std::vector<uint8_t>*);
zx_status_t log_value(const fuchsia::diagnostics::stream::Value&, std::vector<uint8_t>*);
zx_status_t log_argument(const fuchsia::diagnostics::stream::Argument&, std::vector<uint8_t>*);
}  // namespace internal
}  // namespace streams

#endif  // SRC_DIAGNOSTICS_LIB_STREAMS_ENCODE_H_
