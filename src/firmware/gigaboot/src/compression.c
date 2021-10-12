// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compression.h"

#include <inttypes.h>
#include <log.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <lz4/lz4frame.h>

static LZ4F_dctx* lz4_context = NULL;
static const void* compressed_input = NULL;
static size_t compressed_size = 0;

// LZ4 decompression doesn't always want the entire input at once, each time we
// process a chunk it will give us the optimal input size for the next call.
static size_t next_input_size = 0;

// Returns true if we're in the middle of decompression.
static bool decompress_running(void) { return compressed_input != NULL; }

bool decompress_start(const void* input, size_t size) {
  DLOG("Starting decompression of %zu bytes", size);

  if (decompress_running()) {
    ELOG("a decompression is already in progress");
    return false;
  }

  if (!input || size == 0) {
    ELOG("no data to decompress");
    return false;
  }

  LZ4F_errorCode_t result = LZ4F_createDecompressionContext(&lz4_context, LZ4F_VERSION);
  if (LZ4F_isError(result)) {
    ELOG("failed to create LZ4 decompression context: %s", LZ4F_getErrorName(result));
    return false;
  }

  compressed_input = input;
  compressed_size = next_input_size = size;
  return true;
}

void decompress_stop(void) {
  if (lz4_context) {
    LZ4F_errorCode_t result = LZ4F_freeDecompressionContext(lz4_context);
    if (LZ4F_isError(result)) {
      WLOG("decompression did not fully complete: %s", LZ4F_getErrorName(result));
    }
    lz4_context = NULL;
  }

  compressed_input = NULL;
  compressed_size = next_input_size = 0;
}

decompress_result_t decompress_next_chunk(void** output, size_t* size) {
  // 4MiB is the maximum LZ4 block size, always reserve this much space for
  // simplicity. If this becomes an issue we could check the frame for this
  // particular frame's max block size and dynamically allocate instead.
  static uint8_t decompress_buffer[4 * 1024 * 1024];

  if (!decompress_running()) {
    ELOG("no decompression currently running");
    return DECOMPRESS_FAILURE;
  }

  DLOG("Decompressing up to the next %zu bytes", next_input_size);
  size_t source_bytes = next_input_size;
  size_t dest_bytes = sizeof(decompress_buffer);
  size_t result = LZ4F_decompress(lz4_context, decompress_buffer, &dest_bytes, compressed_input,
                                  &source_bytes, NULL);
  if (LZ4F_isError(result)) {
    ELOG("decompression failure (%s)", LZ4F_getErrorName(result));
    return DECOMPRESS_FAILURE;
  }

  DLOG("Decompressed %zu -> %zu bytes", source_bytes, dest_bytes);
  next_input_size = result;
  compressed_input += source_bytes;
  *output = decompress_buffer;
  *size = dest_bytes;
  return (next_input_size ? DECOMPRESS_CONTINUE : DECOMPRESS_FINISHED);
}
