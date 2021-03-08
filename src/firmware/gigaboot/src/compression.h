// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_COMPRESSION_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_COMPRESSION_H_

#include <stdbool.h>
#include <stddef.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// Initializes decompression resources.
//
// Only one decompression can be running at a time, and decompress_stop() must
// be called when finished.
//
// Currently only supports LZ4 decompression but more formats may be added
// in the future.
//
// input: the compressed input.
// size: input size.
//
// Returns true on success.
bool decompress_start(const void* input, size_t size);

// Cleans up any open decompression resources.
// No-op if no decompression is currently running.
void decompress_stop(void);

// Decompression status codes. See decompress_next_chunk() for details.
typedef enum { DECOMPRESS_FAILURE, DECOMPRESS_CONTINUE, DECOMPRESS_FINISHED } decompress_result_t;

// Decompresses the next input chunk.
//
// |output| is a static buffer so contents will change on the next call. The
// caller must handle or copy the decompressed data before then.
//
// output: filled with a pointer to the decompressed data.
// size: filled with the decompressed size.
//
// Returns:
//   DECOMPRESS_FAILURE on error; |output| and |size| will be unchanged.
//   DECOMPRESS_CONTINUE on success when there's more data to process.
//   DECOMPRESS_FINISHED on success when all data has been decompressed.
decompress_result_t decompress_next_chunk(void** output, size_t* size);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_COMPRESSION_H_
