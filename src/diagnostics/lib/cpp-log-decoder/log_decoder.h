// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DIAGNOSTICS_LIB_CPP_LOG_DECODER_LOG_DECODER_H_
#define SRC_DIAGNOSTICS_LIB_CPP_LOG_DECODER_LOG_DECODER_H_
#include <stdint.h>

extern "C" {
// Decodes a structured logging packet to JSON
// The returned string must be freed with fuchsia_free_decoded_log_message.
const char* fuchsia_decode_log_message_to_json(uint8_t* str, uint64_t size);

// Frees a message allocated by fuchsia_decode_log_message_to_json
void fuchsia_free_decoded_log_message(const char* str);
}

#endif  // SRC_DIAGNOSTICS_LIB_CPP_LOG_DECODER_LOG_DECODER_H_
