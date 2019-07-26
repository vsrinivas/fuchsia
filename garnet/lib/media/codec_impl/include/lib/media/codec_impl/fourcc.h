// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_FOURCC_H_
#define GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_FOURCC_H_

#include <endian.h>

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

static inline std::string fourcc_to_string(uint32_t fourcc) {
  // fourcc has first letter in the low-order byte.  We want that letter to be
  // the first byte in memory, without regard for host endian-ness, so convert
  // from host to little-endian which puts the low-order byte first.
  uint32_t little_endian_fourcc = htole32(fourcc);
  return std::string(reinterpret_cast<char*>(&little_endian_fourcc), sizeof(fourcc));
}

#endif  // GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_FOURCC_H_
