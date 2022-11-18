// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_MEMORY_PROFILE_STACK_COMPRESSION_H_
#define SRC_PERFORMANCE_MEMORY_PROFILE_STACK_COMPRESSION_H_

#include <lib/stdcompat/span.h>

#include <cstdint>

// Compresses a 64 bit stack trace into bytes, writes the result in
// `out` and returns the subspan containing the actual result.
//
// This function does not allocate.
//
// `out` should contain at least 9 element per item in `values`,
// howevever the returned span is likely to be much smaller.
//
// The compression method xors the value with the one located before
// if any, and encode the resulting integer with varint coding.
//
// This is assuming that consecutive addresses of the backtrace are in
// the same library and share the same prefix.
//
// The 64 bits value is processed by chunk of 7 bits, LSB first.
// Chucks are outputted with the top bit set to one except for the
// last non zero chunk.
cpp20::span<uint8_t> compress(cpp20::span<const uint64_t> values, cpp20::span<uint8_t> out);

// Decompresses the specified input bytes into `values` and returns the subspan containing
// the actual result.
cpp20::span<uint64_t> decompress(cpp20::span<const uint8_t> in, cpp20::span<uint64_t> values);

#endif  // SRC_PERFORMANCE_MEMORY_PROFILE_STACK_COMPRESSION_H_
