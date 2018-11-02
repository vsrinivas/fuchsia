// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_UTIL_H_
#define GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_UTIL_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fxl/logging.h>

#include <openssl/sha.h>

#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <memory>

#define VLOG_ENABLED 0

#if (VLOG_ENABLED)
#define VLOGF(...) printf(__VA_ARGS__)
#else
#define VLOGF(...) \
  do {             \
  } while (0)
#endif

#define LOGF(...) printf(__VA_ARGS__)

void Exit(const char* format, ...);

std::unique_ptr<uint8_t[]> read_whole_file(const char* filename, size_t* size);

// Post to dispatcher in a way that's guaranteed to run the posted work in the
// same order as the posting order (is the intent - if async::PostTask ever
// changes to not guarantee order, we'll need to work around that here).
//
// TODO(dustingreen): Determine if async::PostTask() intends to strictly
// guarantee order.
void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);

template <typename T>
void UpdateSha256(SHA256_CTX* ctx, T field) {
  T field_le;
  switch (sizeof(field)) {
    case 8:
      field_le = htole64(field);
      break;
    case 4:
      field_le = htole32(field);
      break;
    case 2:
      field_le = htole16(field);
      break;
    case 1:
      field_le = field;
      break;
    default:
      Exit("UpdateSha256 unexpected field size");
  }
  if (!SHA256_Update(ctx, &field_le, sizeof(field_le))) {
    FXL_CHECK(false) << "SHA256_Update() failed in UpdateSha256()";
  }
}

void SHA256_Update_AudioParameters(SHA256_CTX* sha256_ctx,
                                   const fuchsia::mediacodec::PcmFormat& pcm);

void SHA256_Update_VideoParameters(
    SHA256_CTX* sha256_ctx,
    const fuchsia::mediacodec::VideoUncompressedFormat& video);

void SHA256_Update_VideoPlane(SHA256_CTX* sha256_ctx, uint8_t* start,
                              uint32_t width, uint32_t stride, uint32_t height);

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c,
                                             uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

static inline std::string fourcc_to_string(uint32_t fourcc) {
  // fourcc has first letter in the low-order byte.  We want that letter to be
  // the first byte in memory, without regard for host endian-ness, so convert
  // from host to little-endian which puts the low-order byte first.
  uint32_t little_endian_fourcc = htole32(fourcc);
  return std::string(reinterpret_cast<char*>(&little_endian_fourcc),
                     sizeof(fourcc));
}

#endif  // GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_UTIL_H_
